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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION("edu_irq");

#define MAX_EVENTS	   1024
#define EDU_VENDOR     0x1234
#define EDU_DEVICE     0x11e8

#define EDU_IRQ_STATUS 0x24   /* RO: pending bits */
#define EDU_IRQ_RAISE  0x60   /* WO: write any value to raise IRQ */
#define EDU_IRQ_ACK    0x64   /* WO: write same bits from STATUS to clear */



struct edu_irq_device
{
	struct pci_dev *pdev;
	void __iomem *bar0;
	spinlock_t lock; // lock
	atomic_t event_count; // tells
	ktime_t timestamps[MAX_EVENTS];
	atomic_t index;
	wait_queue_head_t waitq; // for the readers waiting for timestamps
	struct work_struct work;

}edu_dev;



static ssize_t short_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/* Basically waits until short_dev.head->ready ... rail if any are ready*/
	struct edu_irq_device *dev = &edu_dev;
	unsigned long flags = 0;
	ktime_t kt;
	u64 ns;
	int len;
	char line[40];
	// make sure there is some data to consume
	if (!atomic_read(&dev->event_count))
	{
		// wait until there is one timestamp to consume
		if (wait_event_interruptible(dev->waitq, atomic_read(&dev->event_count) > 0)) return -ERESTART;
	}
	// Data is available
	spin_lock_irqsave(&dev->lock, flags);
	kt = dev->timestamps[atomic_read(&dev->index) - atomic_read(&dev->event_count)];
	atomic_dec(&dev->event_count); // decrement events
	spin_unlock_irqrestore(&dev->lock, flags);
	ns = ktime_to_ns(kt);
	len = scnprintf(line, sizeof(line), "%llu\n", (unsigned long long)ns);
	if (count < len) return -EINVAL;
	if (copy_to_user(buf, line, len)) return -EFAULT;
	return len;
}

static ssize_t edu_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct edu_irq_device *dev = &edu_dev;
	if (!dev->bar0) return -ENODEV;
	iowrite32(0x1, dev->bar0 + EDU_IRQ_RAISE);
	return count;
}

static const struct file_operations edu_irq_fops = {
	.owner  = THIS_MODULE,
	.read   = short_read,
	.write  = edu_write,
	.llseek = noop_llseek,
};

static struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "eduirq",
	.fops  = &edu_irq_fops,
	.mode  = 0660,
};
static irqreturn_t edu_irq_handler(int irq, void *dev_id)
{
	struct edu_irq_device *dev = dev_id;
	ktime_t tk = ktime_get();
	// latch - check if our device
	u32 st = ioread32(dev->bar0 + EDU_IRQ_STATUS);
	if (!st) return IRQ_NONE;
	// ack - clear the register
	iowrite32(st, dev->bar0 + EDU_IRQ_ACK); // write same status value to ack

	// acquire lock
	spin_lock(&dev->lock);
	dev->timestamps[atomic_read(&dev->index) % (MAX_EVENTS-1)] = tk;
	atomic_inc(&dev->index);
	if (atomic_read(&dev->event_count) < MAX_EVENTS)
		atomic_inc(&dev->event_count);
	spin_unlock(&dev->lock);
	/* handoff */
	schedule_work(&dev->work);

	return IRQ_HANDLED;

}
static void edu_work(struct work_struct *work)
{
	struct edu_irq_device *d = container_of(work, struct edu_irq_device, work);
	/* notify user space */
	if (atomic_read(&d->event_count))
		wake_up_interruptible(&d->waitq);

	pr_info("shortirq work queue (%s): waking up processes\n", current->comm);

}
static const struct pci_device_id edu_ids[] = {
	{PCI_DEVICE(EDU_VENDOR, EDU_DEVICE)},
	{0,},
};
MODULE_DEVICE_TABLE(pci, edu_ids);


static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;

	struct edu_irq_device * dev = &edu_dev;
	// initalize the device here
	spin_lock_init(&dev->lock);
	atomic_set(&dev->event_count, 0);
	atomic_set(&dev->index, 0);
	memset(dev->timestamps, 0, sizeof(dev->timestamps));
	init_waitqueue_head(&dev->waitq);
	INIT_WORK(&dev->work, edu_work);

	ret = pci_enable_device_mem(pdev); // enable bits on the config space
	pci_set_master(pdev); // set command reg so it can do dma/intr
	if (ret) return ret;
	ret = pci_request_mem_regions(pdev, "pci_eduirq");
	if (ret) goto disable_dev;
	dev->bar0 = pci_iomap(pdev, 0, 0); // ioremap the bar0
	if (!dev->bar0)
	{
		ret = -ENODEV;
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
	ret = request_irq(pdev->irq, edu_irq_handler, 0, "eduirq", dev);
	if (ret < 0) goto err_vectors;
	// register char
	ret = misc_register(&miscdev);
	if (ret) goto err_irq;
	pci_set_drvdata(pdev, dev); // pdev->dev = dev
	dev->pdev = pdev;
	dev_info(&pdev->dev, "EDU demo: BAR0 mapped, IRQ %d\n", pdev->irq);
	return 0;
	err_irq: free_irq(pdev->irq, dev);
	err_vectors: pci_free_irq_vectors(pdev);
	unmap: pci_iounmap(pdev, dev->bar0);
	release_mem: pci_release_mem_regions(pdev);
	disable_dev: pci_disable_device(pdev);
	return ret;



}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu_irq_device * dev = pci_get_drvdata(pdev);
	free_irq(pdev->irq, dev);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, dev->bar0);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	misc_deregister(&miscdev);
	cancel_work_sync(&dev->work);

}
static struct pci_driver edu_drv = {
	.name = "edu_irq",
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,

};

module_pci_driver(edu_drv);