#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scull");
MODULE_VERSION("0.2");


static int __init scull_init(void) {
    return 0;
}

static void __exit scull_exit(void) {
    printk(KERN_INFO "Goodbye");
}

module_init(scull_init);
module_exit(scull_exit);

