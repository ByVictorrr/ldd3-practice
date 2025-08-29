#include <linux/module.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>

static struct class *cls;
static struct cdev comp_cdev;
static dev_t dev;

static DECLARE_COMPLETION(completion);

static ssize_t complete_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
    pr_debug("process %i (%s) going to sleep\n", current->pid, current->comm);
    wait_for_completion(&completion);
    pr_info("awoken %i (%s)\n", current->pid, current->comm);
    return 0;
}
static ssize_t complete_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
    pr_debug("process %i (%s) awkening the readers...\n", current->pid, current->comm);
    complete(&completion);
    return count;
}

struct file_operations fops = {
    .read = complete_read,
    .write = complete_write,
};
static int __init init_compl(void){
    int result = alloc_chrdev_region(&dev, 0, 1, "complete");
    int scull_major = MAJOR(dev);
	if (result < 0)
	{
		printk(KERN_WARNING "scull: cant get major %d\n", scull_major);
		return result;
	}
    // create class at /sys/class/scull
	cls = class_create(THIS_MODULE, "completion");
	// associate the cdev with file operations
	cdev_init(&comp_cdev, &fops);
	comp_cdev.owner = THIS_MODULE;
	result = cdev_add(&comp_cdev, dev, 1);
	if (result)
		printk(KERN_NOTICE "scull: cdev_add failed\n");

    device_create(cls, NULL, dev, NULL, "completion%d", 0);
    return 0;
}
static void __exit cleanup_completion(void){
    device_destroy(cls, dev);
    cdev_del(&comp_cdev);
	unregister_chrdev_region(dev, 1);
	class_destroy(cls);
}

module_init(init_compl);
module_exit(cleanup_completion);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("you");
MODULE_DESCRIPTION("completion demo");