// short_intr.c — one /dev/short; select DATA/STATUS/CTRL by file position (0..2)
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


#define DRIVER_NAME "pci_edu"

static int ms_wait_for_dma = 500;
module_param(ms_wait_for_dma, int, 0);

static bool interrupts_enabled = true;
module_param(interrupts_enabled, bool, 0);

#define DMA_BUF_SIZE	4096
#define EDU_VENDOR     0x1234
#define EDU_DEVICE     0x11e8

/* ---BAR 0 register maps---- */

// low MMIO

/* RO: returns version ID */
#define EDU_BAR0_ID_REG 0x00
/* RW: write any 32-bit value; reading returns its bitwise inverse */
#define EDU_BAR0_LIVENESS_REG 0x04
/* RW: write N; device writes back N! after you clear the 'factial busy bit' in STATUS */
#define EDU_BAR0_FACTORIAL_REG 0x08
/* RW: Bit-OR field: 0x01 = factorial busy (RO); 0x80= raise IRQ when factorial completes*/
#define EDU_BAR0_STATUS_REG 0x20
/* RO: latches value written in 'raise' 0x60, you clear vbits via 'ack' (0x64) */
#define EDU_BAR0_IRQ_STATUS_REG 0x24
/* WO: write any value to trigger an IRQ; value is OR'ed into 0x24 */
#define EDU_BAR0_IRQ_RAISE_REG  0x60
/* WO: write same value you saw in 0x24 to drop the interrupt. must be done in ISR */
#define EDU_BAR0_IRQ_ACK_REG    0x64

// DMA engine registers


/* RW: Host physical address */
#define EDU_BAR0_DMA_SRC_REG    0x80
/* RW: Host physical address */
#define EDU_BAR0_DMA_DST_REG    0x88
/* RW: transfer size */
#define EDU_BAR0_DMA_COUNT_REG    0x90
/* RW Bit-OR: 0x01 start, 0x20 dir (0=RAM->EDU, 1=EDU-RAM), 0x04 raise IRQ (0x100 when done; poll bit 0 to wait for completion*/
#define EDU_BAR0_DMA_CMD_REG    0x98

// 4KiB
#define EDU_BAR0_DMA_BUFFER_REG 0x40000

// COMMANDS
#define EDU_DMA_CMD_START    (1u << 0)  /* 0x01: write 1 to start; poll bit 0 for completion */
#define EDU_DMA_CMD_DIR      (1u << 1)  /* 0x02: 0 = RAM→EDU (host→device), 1 = EDU→RAM (device→host) */
#define EDU_DMA_CMD_IRQ      (1u << 2)  /* 0x04: raise IRQ when done */

/* Optional helpers for readability */
#define EDU_DMA_DIR_RAM_TO_DEV   (0u)           /* clear DIR bit */
#define EDU_DMA_DIR_DEV_TO_RAM   (EDU_DMA_CMD_DIR)
#define EDU_DMA_CMD_BUILD(dir/*0/1*/, irq) \
(EDU_DMA_CMD_START | ((dir)?EDU_DMA_CMD_DIR:0) | ((irq)?EDU_DMA_CMD_IRQ:0))

struct edu_dev
{
	struct pci_dev *pdev;
	void __iomem *bar0;
	/* locking & sync */
	struct mutex xfer_lock;      /* serialize DMA starts in process context */
	spinlock_t    isr_lock;      /* protect in_flight/irq-shared fields */
	struct completion done;      /* signals DMA completion */
	bool in_flight;
	/* dma address */
	dma_addr_t dma_handle;
	void * dma_buf;

};

static int edu_dma_transfers(struct edu_dev *ed, bool to_device, size_t size)
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


static ssize_t edu_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/* Basically waits until short_dev.head->ready ... rail if any are ready*/
	int ret;
	struct edu_dev *dev = filp->private_data;
	count = min(count, DMA_BUF_SIZE);
	ret = edu_dma_transfers(dev, false, count);
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
	ret = edu_dma_transfers(dev, true, count);
	if (ret) return ret;
	return count;
}

static struct miscdevice miscdev;
static int edu_open(struct inode *inode, struct file *filp)
{
	struct edu_dev *dev = dev_get_drvdata(miscdev.this_device);
	if (!dev) return -ENODEV;
	filp->private_data = dev;
	return 0;
}

static const struct file_operations edu_irq_fops = {
	.owner  = THIS_MODULE,
	.open   = edu_open,
	.read   = edu_read,
	.write  = edu_write,
	.llseek = noop_llseek,
};
static struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "edu",
	.fops  = &edu_irq_fops,
	.mode  = 0660,
};


static irqreturn_t edu_irq_handler(int irq, void *dev_id)
{
	struct edu_dev *dev = dev_id;
	// latch - check if our device
	u32 st = ioread32(dev->bar0 + EDU_BAR0_IRQ_STATUS_REG);
	if (!st) return IRQ_NONE;
	// ack - clear the register
	iowrite32(st, dev->bar0 + EDU_BAR0_IRQ_ACK_REG); // write same status value to ack
	spin_lock(&dev->isr_lock);
	// in flight means one is waiting for compeltion
	if (dev->in_flight)
		complete(&dev->done);
	spin_unlock(&dev->isr_lock);
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
	ret = request_irq(pdev->irq, edu_irq_handler, 0, DRIVER_NAME, dev);
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
	dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, dev->dma_buf, dev->dma_handle);
	free_irq(pdev->irq, dev);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, dev->bar0);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	misc_deregister(&miscdev);

}
static struct pci_driver edu_drv = {
	.name = DRIVER_NAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,

};

module_pci_driver(edu_drv);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION(DRIVER_NAME);