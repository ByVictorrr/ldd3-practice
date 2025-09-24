#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include "scull.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scull");
MODULE_VERSION("0.2");
int scull_major = SCULL_MAJOR;
module_param(scull_major, int, 0444);
MODULE_PARM_DESC(scull_major, "Major number");

int scull_minor = SCULL_MINOR;
module_param(scull_minor, int, 0444);
MODULE_PARM_DESC(scull_minor, "Minor number");

int scull_nr_devs = SCULL_NR_DEVS;
module_param(scull_nr_devs, int, 0444);
MODULE_PARM_DESC(scull_nr_devs, "Number of SCULL Devices");

int scull_qset = SCULL_QSET;
module_param(scull_qset, int, 0444);
MODULE_PARM_DESC(scull_qset, "How large should the qset be?");

int scull_quantum = SCULL_QUANTUM;
module_param(scull_quantum, int, 0444);
MODULE_PARM_DESC(scull_quantum, "How large should the quantum be?");


struct class * cls;
struct scull_dev *scull_devices;
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	dev_t devno = MKDEV(scull_major, scull_minor + index);
	// associate the cdev with file operations
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_NOTICE "scull: cdev_add failed\n");
}

static void _scull_cleanup_module(void)
{
	dev_t devno = MKDEV(scull_major, scull_minor);
	int i;
	/* Get rid of our char dev enteries */

	if (scull_devices)
	{
		for (i=0; i < scull_nr_devs; i++)
		{
			scull_dev_reset(&scull_devices[i]);
			cdev_del(&scull_devices[i].cdev);
			device_destroy(cls, MKDEV(MAJOR(devno), scull_minor + i));
		}
		kfree(scull_devices);
	}
	unregister_chrdev_region(devno, scull_nr_devs);
	class_destroy(cls);
}
static int __init scull_init(void) {
	dev_t dev;
	int result, i;

	if (scull_major)
	{
		/*
		* With given (major, start_minor) register scull_nr_devs with name "scull" in /proc/devices
		*/
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scull");
	}else
	{
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0)
	{
		printk(KERN_WARNING "scull: cant get major %d\n", scull_major);
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
	cls = class_create("scull");
	for (i=0; i<scull_nr_devs; i++)
	{
		device = &scull_devices[i];
		device->quantum = scull_quantum;
		device->qset    = scull_qset;
		device->size    = 0;
		device->data    = NULL;
		sema_init(&device->sem, 1);
		scull_setup_cdev(device, i);
		device_create(cls, NULL, MKDEV(scull_major, scull_minor + i), NULL, "scull%d", i);
	}

	return 0;

	fail:
	_scull_cleanup_module();
	return result;
}


static void __exit scull_exit(void) {
	_scull_cleanup_module();
}

module_init(scull_init);
module_exit(scull_exit);

