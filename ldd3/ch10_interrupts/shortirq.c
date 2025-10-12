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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION("shortirq");

#define MAX_EVENTS 1024
#define IRQ_NUMBER 7


enum bh_type {
	BH_TASKLET = 0,
	BH_THREADED,
	BH_WORKQUEUE,
};

static enum bh_type bh_mode = BH_TASKLET;
static const char * const bh_modes[] = {
	[BH_TASKLET]   = "tasklet",
	[BH_THREADED]  = "threaded",
	[BH_WORKQUEUE] = "workqueue",
};

/* Parser for module_param */
static int param_set_bh_mode(const char *val, const struct kernel_param *kp)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(bh_modes); i++) {
		if (sysfs_streq(val, bh_modes[i])) {
			bh_mode = i;
			return 0;
		}
	}
	pr_err("Invalid bh_mode '%s' (valid: tasklet, threaded, workqueue)\n", val);
	return -EINVAL;
}

static int param_get_bh_mode(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%s\n", bh_modes[bh_mode]);
}
static unsigned long poke_base = 0x2000;     /* port base for poking the IRQ line */
module_param_call(bh_mode, param_set_bh_mode, param_get_bh_mode, &bh_mode, 0644);
MODULE_PARM_DESC(bh_mode, "Bottom-half mode: tasklet, threaded, or workqueue");





struct edu_irq_device
{
	spinlock_t lock; // lock
	atomic_t event_count; // tells
	ktime_t timestamps[MAX_EVENTS];
	atomic_t index;
	int irq; // irq number
	wait_queue_head_t waitq; // for the readers waiting for timestamps
	/* BH options */
	struct tasklet_struct tasklet;
	struct work_struct work;

}shortirq_dev;



static ssize_t short_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/* Basically waits until short_dev.head->ready ... rail if any are ready*/
	struct edu_irq_device *dev = &shortirq_dev;
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
	struct edu_irq_device *dev = &shortirq_dev;
	outb(1, poke_base + dev->irq);            // assert IRQ7
	outb(0, poke_base + dev->irq);            // deassert IRQ7 (edge/pulse)
	return count;
}

static const struct file_operations shortirq_fops = {
	.owner  = THIS_MODULE,
	.read   = short_read,
	.write  = edu_write,
	.llseek = noop_llseek,
};

static struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "shortirq",
	.fops  = &shortirq_fops,
	.mode  = 0660,
};
static irqreturn_t edu_irq_handler(int irq, void *dev_id)
{
	struct edu_irq_device *dev = dev_id;
	ktime_t tk = ktime_get();
	// acquire lock
	spin_lock(&dev->lock);

	dev->timestamps[atomic_read(&dev->index) % (MAX_EVENTS-1)] = tk;
	atomic_inc(&dev->index);
	if (atomic_read(&dev->event_count) < MAX_EVENTS)
		atomic_inc(&dev->event_count);
	spin_unlock(&dev->lock);
	/* handoff */
	if (bh_mode == BH_THREADED) return IRQ_WAKE_THREAD;
	if (bh_mode == BH_WORKQUEUE)
	{
		schedule_work(&dev->work);
	} else
	{
		tasklet_schedule(&dev->tasklet);
	}

	return IRQ_HANDLED;

}
static irqreturn_t shortirq_threaded_handler(int irq, void *dev_id)
{
	struct edu_irq_device *d = dev_id;
	/* notify user space */
	if (atomic_read(&d->event_count))
		wake_up_interruptible(&d->waitq);

	pr_info("shortirq threaded handler (%s): waking up processes\n", current->comm);
	return IRQ_HANDLED;
}
static void shortirq_tasklet(unsigned long data)
{

	struct edu_irq_device *d = (struct edu_irq_device *)data;
	/* notify user space */
	if (atomic_read(&d->event_count))
		wake_up_interruptible(&d->waitq);
	pr_info("shortirq tasklet (%s): waking up processes\n", current->comm);

}
static void edu_work(struct work_struct *work)
{
	struct edu_irq_device *d = container_of(work, struct edu_irq_device, work);
	/* notify user space */
	if (atomic_read(&d->event_count))
		wake_up_interruptible(&d->waitq);

	pr_info("shortirq work queue (%s): waking up processes\n", current->comm);

}

static int __init short_init(void)
{
	int ret;

	struct edu_irq_device * dev = &shortirq_dev;
	dev->irq = IRQ_NUMBER;
	unsigned long port = poke_base + shortirq_dev.irq;
	/* Port I/O: reserve ports; no mapping */
	if (!request_region(port, 1,"shortirq")) return -EBUSY;
	ret = misc_register(&miscdev);
	if (ret) {
		release_region(port, 1);
		return ret;
	}
	/* request thread */
	ret = request_threaded_irq(dev->irq, edu_irq_handler,
		(bh_mode == BH_THREADED) ? shortirq_threaded_handler: NULL, IRQF_SHARED, "shortirq", dev);
	if (ret)
	{
		misc_deregister(&miscdev);
		release_region(port, 1);
	}
	// initalize the device here
	spin_lock_init(&dev->lock);
	atomic_set(&dev->event_count, 0);
	atomic_set(&dev->index, 0);
	memset(dev->timestamps, 0, sizeof(dev->timestamps));
	init_waitqueue_head(&dev->waitq);
	INIT_WORK(&dev->work, edu_work);
	tasklet_init(&dev->tasklet, shortirq_tasklet, (unsigned long)dev);

	pr_info("shortirq: port I/O at 0x%lx as /dev/short; ", port);
	return 0;
}

static void __exit short_exit(void)
{
	struct edu_irq_device * dev = &shortirq_dev;
	free_irq(dev->irq, dev);
	misc_deregister(&miscdev);
	release_region(poke_base + shortirq_dev.irq, 1);
	tasklet_kill(&dev->tasklet);
	cancel_work_sync(&dev->work);
}

module_init(short_init);
module_exit(short_exit);
