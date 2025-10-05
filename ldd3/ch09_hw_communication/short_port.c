#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/container_of.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/io.h>




MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scullc");
MODULE_VERSION("0.2");
static unsigned long shortp_base = 0x378;
module_param(shortp_base, long, 0444);
MODULE_PARM_DESC(short_base, "Physical address for port I/O");




#define SPAN 3

static loff_t shortp_llseek(struct file *f, loff_t off, int whence)
{
	loff_t pos;

	switch (whence)
	{
		case SEEK_SET: pos = off; break;
		case SEEK_CUR: pos = f->f_pos + off; break;
		case SEEK_END: pos = (SPAN - 1) + off; break;
		default: return -EINVAL;
	}
	if (pos < 0 || pos > 2) return -EINVAL;
	return pos;
}


ssize_t shortp_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{

    unsigned long port = shortp_base + iminor(file_inode(filp));
    u8 value = inb(port); // read a byte from the port (x86 provides memory barriers internally)
    //copy the value to user-space
    if (copy_to_user(buf, &value, 1)) return -EFAULT;
    return 1; // one byte read
}
ssize_t shortp_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    unsigned long port = shortp_base + iminor(file_inode(filp));
    u8 value;
    if (copy_from_user(&value, buf, 1)) return -EFAULT;
    outb(value, port); // write the byte to the port
    return 1; // one byte writen
}
const static struct file_operations _fops ={
    .owner = THIS_MODULE,
    .read = scull_read,
    .write = scull_write,
    .llseek = no_llseek,
};
static struct miscdevice  shorp_device = {
	.minor = MISC_DYNAMIC_MINOR,


};



static int __init shortp_init(void) {
	dev_t dev;
	int result, i;

	if (scull_major)
	{
		/*
		* With given (major, start_minor) register scull_nr_devs with name "scull" in /proc/devices
		*/
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scullc");
	}else
	{
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scullc");
		scull_major = MAJOR(dev);
	}
	if (result < 0)
	{
		printk(KERN_WARNING "scullc: cant get major %d\n", scull_major);
		return result;
	}
	/* GFP_KERNEL */
	scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices)
	{
		result = -ENOMEM;
		goto fail;
	}
	struct scull_dev *device;

	// create class at /sys/class/scull
	cls = class_create("scullc");
	for (i=0; i<scull_nr_devs; i++)
	{
		device = &scull_devices[i];
		device->quantum = scull_quantum;
		device->qset    = scull_qset;
		device->size    = 0;
		device->data    = NULL;
		sema_init(&device->sem, 1);
		scull_setup_cdev(device, i);
		device_create(cls, NULL, MKDEV(scull_major, scull_minor + i), NULL, "scullc%d", i);
	}
    scullc_cache = kmem_cache_create("scullc", scull_quantum, 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!scullc_cache)
    {
        result = -ENOMEM;
        goto fail;
    }

	return 0;

	fail:
	_scull_cleanup_module();
	return result;
}


static void __exit short_exit(void) {
	_scull_cleanup_module();
}

module_init(short_init)
module_exit(short_exit)