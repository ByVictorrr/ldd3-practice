#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kfifo.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include "edu.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION("EDU PCI Driver - learning in chapter 12");


static int ms_wait_for_dma = 500;
module_param(ms_wait_for_dma, int, 0);
MODULE_PARM_DESC(ms_wait_for_dma, "Waiting for DMA transfer");

static bool interrupts_enabled = true;
module_param(interrupts_enabled, bool, 0);
MODULE_PARM_DESC(interrupts_enabled, "Enable interrupts");



struct miscdevice miscdev;

static int edu_open(struct inode *inode, struct file *filp)
{
	struct edu_dev *dev = dev_get_drvdata(miscdev.this_device);
	filp->private_data = dev;
	dev->ms_wait_for_dma = ms_wait_for_dma;
	dev->interrupts_enabled = interrupts_enabled;

	return 0;
}
static const struct file_operations uac_fops = {
	.owner  = THIS_MODULE,
	.open   = edu_open,
	.read   = edu_read,
	.write  = edu_write,
	.llseek = noop_llseek,
};

struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "edu",
	.fops  = &uac_fops,
	.mode  = 0660,
};


const struct pci_device_id edu_ids[] = {
	{PCI_DEVICE(EDU_VENDOR, EDU_DEVICE)},
	{0,},
};

MODULE_DEVICE_TABLE(pci, edu_ids);

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

static int __init edu_init(void)
{
	return pci_register_driver(&edu_drv);
}
static void __exit edu_exit(void)
{
	pci_unregister_driver(&edu_drv);
}

module_init(edu_init);
module_exit(edu_exit);

//module_pci_driver(edu_drv);
