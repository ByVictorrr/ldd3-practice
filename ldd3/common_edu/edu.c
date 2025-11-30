#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kfifo.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include "edu.h"

#define DRIVER_NAME "pci_edu"

static int ms_wait_for_dma = 500;
module_param(ms_wait_for_dma, int, 0);

static bool interrupts_enabled = true;
module_param(interrupts_enabled, bool, 0);



int edu_dma_transfers(struct edu_dev *ed, bool to_device, size_t size)
{
	int ret = 0;
	u32 src, dst;
	u8 dir;
	if (size > DMA_BUF_SIZE) return -EINVAL;
	// one transfer at a time
	if (mutex_lock_interruptible(&ed->xfer_lock))
		return -ERESTARTSYS;
	// mark in flight so isr knows a dma is active
	spin_lock_irq(&ed->isr_lock);

	if (ed->in_flight)
	{
		spin_unlock(&ed->isr_lock);
		mutex_unlock(&ed->xfer_lock);
		return -EBUSY;

	}
	ed->in_flight = true;
	reinit_completion(&ed->done);
	spin_unlock_irq(&ed->isr_lock);

	// mutex here is holding only
	if (to_device)
	{
		// RAM -> EDU (TX)
		src = lower_32_bits(ed->dma_handle);
		dst = EDU_BAR0_DMA_BUFFER_REG;
		dir = 0;
	}else
	{
		// EDU -> RAM (RX)
		src = EDU_BAR0_DMA_BUFFER_REG;
		dst = lower_32_bits(ed->dma_handle);
		dir = 1;
	}
	iowrite32(dst, ed->bar0 + EDU_BAR0_DMA_DST_REG);
	iowrite32(src, ed->bar0 + EDU_BAR0_DMA_SRC_REG);
	iowrite32(size, ed->bar0 + EDU_BAR0_DMA_COUNT_REG);
	wmb(); // ensure above get written before the cmd
	iowrite32(EDU_DMA_CMD_BUILD(dir, interrupts_enabled), ed->bar0 + EDU_BAR0_DMA_CMD_REG);

	if (!interrupts_enabled)
	{
		// poll for ocmption (bit 0 clears) with a timeout
		unsigned long timeout = jiffies + msecs_to_jiffies(ms_wait_for_dma);
		while (time_before(jiffies, timeout)){
			if (!(ioread32(ed->bar0 + EDU_BAR0_DMA_CMD_REG) & EDU_DMA_CMD_START) )
				goto done;
			cpu_relax();
		}
		ret = -ETIMEDOUT;

	}else
	{
		if (!wait_for_completion_timeout(&ed->done, msecs_to_jiffies(ms_wait_for_dma)))
		{
			ret = -ETIMEDOUT;
		}
	}
	done:
	/* Clear in_flight and release locks */
	spin_lock_irq(&ed->isr_lock);
	ed->in_flight = false;
	spin_unlock_irq(&ed->isr_lock);
	mutex_unlock(&ed->xfer_lock);
	return ret;

}


ssize_t edu_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/* Basically waits until short_dev.head->ready ... rail if any are ready*/
	int ret;
	struct edu_dev *dev = filp->private_data;
	count = min(count, DMA_BUF_SIZE);
	// resume device & increment if sucessful
	ret = pm_runtime_resume_and_get(&dev->pdev->dev);
	if (ret < 0) return ret;
	ret = edu_dma_transfers(dev, false, count);
	// mark last busy for work_delayed item that would run runtime_suspend
	pm_runtime_mark_last_busy(&dev->pdev->dev);
	pm_runtime_put_autosuspend(&dev->pdev->dev);
	if (ret) return ret;
	if (copy_to_user(buf, dev->dma_buf, count)) return -EFAULT;
	return count;
}

static ssize_t edu_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct edu_dev *dev = filp->private_data;
	int ret;
	count = min(count, DMA_BUF_SIZE);
	if (copy_from_user(dev->dma_buf, buf, count)) return -EFAULT;
	// resume device & increment if sucessful
	ret = pm_runtime_resume_and_get(&dev->pdev->dev);
	if (ret <0) return ret;
	ret = edu_dma_transfers(dev, true, count);
	// mark last busy for work_delayed item that would run runtime_suspend
	pm_runtime_mark_last_busy(&dev->pdev->dev);
	pm_runtime_put_autosuspend(&dev->pdev->dev);
	if (ret) return ret;
	return count;
}

static struct miscdevice miscdev;
static int edu_open(struct inode *inode, struct file *filp)
{
	struct edu_dev *dev = dev_get_drvdata(miscdev.this_device);
	filp->private_data = dev;

	return 0;
}

static const struct file_operations uac_fops = {
	.owner  = THIS_MODULE,
	.open   = edu_open,
	.read   = edu_read,
	.write  = edu_write,
	.llseek = noop_llseek,
};
static struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "edu",
	.fops  = &uac_fops,
	.mode  = 0660,
};


static irqreturn_t edu_irq_threaded(int irq, void *dev_id)
{
	struct edu_dev *dev = dev_id;
	struct device *kdev = &dev->pdev->dev;

	int ret = pm_runtime_resume_and_get(kdev); // resume & inc if sucessful
	if (ret < 0) return IRQ_NONE;
	// latch - check if our device
	u32 st = ioread32(dev->bar0 + EDU_BAR0_IRQ_STATUS_REG);
	if (!st)
	{
		/* Not ours: drop the ref we took above. Use _noidle to avoid
		* perturbing autosuspend timing since no real work happened. */
		pm_runtime_put_noidle(kdev);
		return IRQ_NONE;
	}
	// ack - clear the register
	iowrite32(st, dev->bar0 + EDU_BAR0_IRQ_ACK_REG); // write same status value to ack
	spin_lock(&dev->isr_lock);
	// in flight means one is waiting for compeltion
	if (dev->in_flight)
		complete(&dev->done);
	spin_unlock(&dev->isr_lock);
	/* we did the real work; refresh idel timer and allow autosuspend later */
	pm_runtime_mark_last_busy(kdev);
	pm_runtime_put_autosuspend(kdev);
	return IRQ_HANDLED;

}


static const struct pci_device_id edu_ids[] = {
	{PCI_DEVICE(EDU_VENDOR, EDU_DEVICE)},
	{0,},
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	u32 reg;

	struct edu_dev * dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	// initalize the device here
	mutex_init(&dev->xfer_lock);
	spin_lock_init(&dev->isr_lock);
	init_completion(&dev->done);
	dev->in_flight = false;
	// enable bits on the config space CONFIG.MEM decode for the device
	ret = pci_enable_device_mem(pdev);
	// set command reg so it can do dma/intr COMMAND.MASTER
	pci_set_master(pdev);
	if (ret) return ret;
	// request resource phys regions
	ret = pci_request_mem_regions(pdev, DRIVER_NAME);
	if (ret) goto disable_dev;
	dev->bar0 = pci_iomap(pdev, 0, 0); // ioremap the bar0
	if (!dev->bar0)
	{
		ret = -ENODEV;
		goto release_mem;
	}
	// verification 1: read the id reg - verify edu & minor/major
	reg = ioread32(dev->bar0 + EDU_BAR0_ID_REG);
	// verify we are talking to the edu
	if ((reg & 0xFF) != 0xED)
	{
		dev_err(&pdev->dev, "EDU signature mismatch: got 0x%08x\n", reg & 0xFF);
		goto release_mem;
	}
	// verify the major != 0 && minor != 0
	if (((reg >> 24) & 0xFF) == 0 && ((reg >> 16) & 0xFF) == 0){
		dev_err(&pdev->dev, "EDU rev: major=0 & minor = 0");
		goto release_mem;

	}
	// verification 2: liveness register to confirm mmio
	iowrite32(~0u, dev->bar0 + EDU_BAR0_LIVENESS_REG);
	reg = ioread32(dev->bar0 + EDU_BAR0_LIVENESS_REG);
	if (reg != 0)
	{
		dev_err(&pdev->dev, "EDU liveness: wrote 1's and expected 0, but got %ul", reg);
		goto release_mem;
	}

	// here we can request irq vectors because bar0 is mapped -> msi-x table is in bar0
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (ret < 0)
	{
		ret = -ENODEV;
		goto unmap; // if no vectors
	}
	// request irq
	pdev->irq = pci_irq_vector(pdev, 0); // get the irq thats mapped first vector entry in the var
	ret = request_threaded_irq(pdev->irq, NULL, edu_irq_threaded, IRQF_ONESHOT, DRIVER_NAME, dev);
	if (ret < 0) goto err_vectors;
	/* DMA Transfer */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) goto err_irq;
	dev->dma_buf = dma_alloc_coherent(&pdev->dev, DMA_BUF_SIZE, &dev->dma_handle, GFP_KERNEL);
	if (!dev->dma_buf)
	{
		ret = -ENOMEM;
		goto err_irq;
	}
	/* Register char driver */
	ret = misc_register(&miscdev);
	if (ret)
		goto err_free_dma;
	pci_set_drvdata(pdev, dev);
	dev_set_drvdata(miscdev.this_device, dev);
	dev->pdev = pdev;
	/* PM Runtime options */
	pm_runtime_enable(&pdev->dev);  	// // enables runtime power managemnt for this device
	pm_runtime_set_active(&pdev->dev);  // tells pm core that the dev is currently active
	pm_runtime_set_autosuspend_delay(&pdev->dev, 5000); // wait for 5s auto delay
	pm_runtime_use_autosuspend(&pdev->dev); // tell pm core to use autosuspend
	pm_runtime_mark_last_busy(&pdev->dev); // reset timer; last used
	pm_runtime_put_autosuspend(&pdev->dev); // decrement usage count


	dev_info(&pdev->dev, "EDU demo: BAR0 mapped, IRQ %d\n", pdev->irq);
	return 0;
	err_free_dma: dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, dev->dma_buf, dev->dma_handle);
	err_irq: free_irq(pdev->irq, dev);
	err_vectors: pci_free_irq_vectors(pdev);
	unmap: pci_iounmap(pdev, dev->bar0);
	release_mem: pci_release_mem_regions(pdev);
	disable_dev: pci_disable_device(pdev);
	return ret;



}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu_dev * dev = pci_get_drvdata(pdev);
	// free runtime power management
	pm_runtime_get_sync(&pdev->dev); // make sure .resume is called & usage_count = 1
	pm_runtime_barrier(&pdev->dev);     // ensure no pending runtime work
	pm_runtime_disable(&pdev->dev); // disable runtime pm feature
	pm_runtime_put_noidle(&pdev->dev); // set usage_count = 0 with no trigger for suspend
	dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, dev->dma_buf, dev->dma_handle);
	free_irq(pdev->irq, dev);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, dev->bar0);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	misc_deregister(&miscdev);

}
static int edu_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct edu_dev *priv = pci_get_drvdata(pdev);
	u32 st;

	/* Block new DMA submissions and implicitly wait for any in-flight DMA to finish */
	mutex_lock(&priv->xfer_lock);

	/* Clear any latched IRQ status to avoid immediate wake */
	st = ioread32(priv->bar0 + EDU_BAR0_IRQ_STATUS_REG);
	if (st) iowrite32(st, priv->bar0 + EDU_BAR0_IRQ_ACK_REG);

	mutex_unlock(&priv->xfer_lock);
	return 0;
}

static int edu_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct edu_dev * priv = pci_get_drvdata(pdev);
	u32 st;
	/* Clear/ACK any sticky pending status from before sleep */
	st = ioread32(priv->bar0 + EDU_BAR0_IRQ_STATUS_REG);
	if (st) iowrite32(st, priv->bar0 + EDU_BAR0_IRQ_ACK_REG);
	return 0;
}
static struct dev_pm_ops edu_pm_ops = {
	.suspend = edu_pm_suspend,
	.resume = edu_pm_resume,
	.runtime_resume = edu_pm_resume,
	.runtime_suspend = edu_pm_suspend,

};
/* ===================== AER CALLBACKS (annotated) ===================== */

/*
 * .error_detected(): FIRST callback.
 *
 * Called in process context by the AER service when an error is reported.
 * What you can assume:
 *  - Your driver is still bound.
 *  - For state == pci_channel_io_normal (non-fatal):
 *      * Link is up, config space readable, MMIO generally still works.
 *  - For state == pci_channel_io_frozen (fatal):
 *      * Endpoint is considered "frozen" (treat MMIO as unsafe; reads may time out).
 *      * A reset of the downstream port will follow if anyone returns NEED_RESET.
 *  - For state == pci_channel_io_perm_failure:
 *      * Device considered unrecoverable; you should return DISCONNECT.
 *
 * What you should do here:
 *  - Quiesce the device: stop DMA/queues, mask/disable device interrupts, cancel work.
 *  - Save any SW state you need to restore later.
 *  - DO NOT talk much to the device for 'frozen'—just shut things down.
 *  - Choose a return code:
 *      CAN_RECOVER   -> try to continue without a slot reset (non-fatal flows).
 *      NEED_RESET    -> ask PCI core to reset the slot (common & safe choice).
 *      DISCONNECT    -> give up; the core will unbind your driver.
 */
static pci_ers_result_t edu_error_detected(struct pci_dev *pdev,
                                           pci_channel_state_t state)
{
    struct edu_dev *priv = pci_get_drvdata(pdev);
    u32 st;

	// step 2: quiecese software: stop timers, workqueues, etc,

    dev_warn(&pdev->dev, "AER: error_detected(state=%d), quiescing\n", state);
	if (state == pci_channel_io_perm_failure) {
		dev_warn(&pdev->dev, "AER: permanent failure -> DISCONNECT\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	if (state == pci_channel_io_frozen) {
		/* Fatal path: ask for a reset of the downstream port */
		return PCI_ERS_RESULT_NEED_RESET;
	}
	// step: 1 mask vector table entry - touches mmio
	disable_irq_nosync(pdev->irq);

    /* Block new I/O and implicitly wait for any in-flight DMA to drain */
    mutex_lock(&priv->xfer_lock);

    /* Mask/ack any pending device interrupts so we don't re-enter */
    st = ioread32(priv->bar0 + EDU_BAR0_IRQ_STATUS_REG);
    if (st)                                        /* NOTE: your original code had this inverted */
        iowrite32(st, priv->bar0 + EDU_BAR0_IRQ_ACK_REG);

    /* Stop queues / mark quiesced */
    priv->in_flight = false;

    mutex_unlock(&priv->xfer_lock);

    /* Non-fatal path: MMIO likely still OK; allow mmio_enabled() to sanity-check */
    return PCI_ERS_RESULT_CAN_RECOVER;
}

/*
 * .mmio_enabled(): Called ONLY on the non-fatal path after AER has re-enabled
 * MMIO decoding for the device. No slot reset occurred.
 *
 * What’s enabled now:
 *  - MMIO BARs are accessible again.
 *  - Link is up; config space is fine.
 * Not guaranteed:
 *  - Device’s internal state may be inconsistent; you must validate.
 *
 * What to do:
 *  - Probe a couple of safe registers to verify the device is responsive.
 *  - If checks fail, you can still request a reset by returning NEED_RESET.
 *  - If OK, return RECOVERED and defer full restart to .resume().
 */
static pci_ers_result_t edu_mmio_enabled(struct pci_dev *pdev)
{
    struct edu_dev *priv = pci_get_drvdata(pdev);
    u32 reg;

    /* Liveness check: write ~0, expect readback inversion semantics to hold */
    iowrite32(~0u, priv->bar0 + EDU_BAR0_LIVENESS_REG);
    reg = ioread32(priv->bar0 + EDU_BAR0_LIVENESS_REG);
    if (reg != 0) {
        dev_err(&pdev->dev,
                "AER: liveness check failed after mmio_enabled (0x%x)\n", reg);
        return PCI_ERS_RESULT_NEED_RESET;
    }

    return PCI_ERS_RESULT_RECOVERED;
}

/*
 * .slot_reset(): Called after the PCI core resets the downstream port (fatal path)
 * and restores CONFIG SPACE (Command/BARs/MSI/MSI-X state) from saved PCI state.
 *
 * What is guaranteed by PCI core:
 *  - Bus is retrained, device present, config space restored via pci_restore_state().
 *  - Your driver is still bound, but device INTERNAL REGISTERS are back to power-on.
 *
 * What you should do:
 *  - Re-enable the function (pci_enable_device()) if needed.
 *  - pci_set_master() again (DMA).
 *  - Re-map BARs if you didn’t use managed mapping, and fully re-initialize the device:
 *      clear/ack status, reprogram device registers, rebuild rings/queues,
 *      re-arm interrupts, restore DMA buffers, etc.
 *  - Return RECOVERED if ready; DISCONNECT if bring-up fails.
 */
static pci_ers_result_t edu_slot_reset(struct pci_dev *pdev)
{
    struct edu_dev *priv = pci_get_drvdata(pdev);
    u32 reg;

    if (pci_enable_device(pdev)) {
        dev_warn(&pdev->dev, "AER: pci_enable_device failed after reset\n");
        return PCI_ERS_RESULT_DISCONNECT;
    }
    pci_set_master(pdev);

    /* Device is at power-on state: re-init the minimal sanity */
    iowrite32(~0u, priv->bar0 + EDU_BAR0_LIVENESS_REG);
    reg = ioread32(priv->bar0 + EDU_BAR0_LIVENESS_REG);
    if (reg != 0) {
        dev_err(&pdev->dev, "AER: post-reset liveness failed (0x%x)\n", reg);
        return PCI_ERS_RESULT_DISCONNECT;
    }

    /* Re-arm device interrupts if you masked them during quiesce
     * (for EDU, you may need to clear/ack stale bits, then enable) */
	enable_irq(pdev->irq);
    return PCI_ERS_RESULT_RECOVERED;
}

/*
 * .resume(): Called LAST if the previous step returned RECOVERED.
 *
 * What’s true now:
 *  - For non-fatal: MMIO is on; no slot reset occurred.
 *  - For fatal: a reset occurred and you returned RECOVERED from slot_reset.
 *
 * What to do:
 *  - Restart normal operation: unmask device interrupts, start DMA/queues,
 *    schedule workers, etc.
 */
static void edu_resume(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "AER: resume(): normal operations.\n");
    /* Re-start your submit/completion paths here */
}

static struct pci_error_handlers edu_err_handlers = {
	.error_detected = edu_error_detected,
	.mmio_enabled = edu_mmio_enabled,
	.slot_reset = edu_slot_reset,
	.resume = edu_resume,
};


static struct pci_driver edu_drv = {
	.name = DRIVER_NAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,
	.err_handler = &edu_err_handlers,
	.driver = {
		.pm = &edu_pm_ops

	},


};

module_pci_driver(edu_drv);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION(DRIVER_NAME);