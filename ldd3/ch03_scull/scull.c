#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/semaphore.h>
#include <linux/fs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scull");
MODULE_VERSION("0.2");

struct scull_quantum{
	void *data;
	int size;
};
struct scull_qset{
	struct scull_qset *next;
	struct scull_quantum *quantum;

};
struct scull_dev{
	struct scull_qset *qset;
    int quantum;
    int qset;
	unsigned long size;
	unsigned int access_key;
	struct semaphore sem;
	struct cdv dev;


};
struct file_operations scull_fops ={
    .owner = THIS_MODULE,
    .write = scull_write,
    .read = scull_read,
    .open = scull_open,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
    .release = scull_release,
};


static int __init scull_init(void) {
    return 0;
}

static void __exit scull_exit(void) {
    printk(KERN_INFO "Goodbye");
}

module_init(scull_init);
module_exit(scull_exit);

