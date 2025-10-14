#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/rwsem.h>
static struct class *cls;
static struct cdev rwsem_cdev;
static dev_t dev;
W
static DECLARE_RWSEM(rwsem_lock);

static ssize_t read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
    pr_debug("process %i (%s) going to sleep\n", current->pid, current->comm);
    down_read(&rwsem_lock);
    pr_info("awoken %i (%s)\n", current->pid, current->comm);
	up_read(&rwsem_lock);
    return 0;
}
static ssize_t write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	down_write(&rwsem_lock);
    pr_debug("process %i (%s) awkening the readers...\n", current->pid, current->comm);
	up_write(&rwsem_lock);
    return count;
}

struct file_operations fops = {
    .read = read,
    .write = write,
};
static int __init init_compl(void){
    int result = alloc_chrdev_region(&dev, 0, 1, "rwsem");
    int scull_major = MAJOR(dev);
	if (result < 0)
	{
		printk(KERN_WARNING "scull: cant get major %d\n", scull_major);
		return result;
	}
    // create class at /sys/class/scull
	cls = class_create("rwsem");
	// associate the cdev with file operations
	cdev_init(&rwsem_cdev, &fops);
	rwsem_cdev.owner = THIS_MODULE;
	result = cdev_add(&rwsem_cdev, dev, 1);
	if (result)
		printk(KERN_NOTICE "scull: cdev_add failed\n");

    device_create(cls, NULL, dev, NULL, "rwsem%d", 0);
    return 0;
}
static void __exit cleanup_completion(void){
    device_destroy(cls, dev);
    cdev_del(&rwsem_cdev);
	unregister_chrdev_region(dev, 1);
	class_destroy(cls);
}

module_init(init_compl);
module_exit(cleanup_completion);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("you");
MODULE_DESCRIPTION("completion demo");