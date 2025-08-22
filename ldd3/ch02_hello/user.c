#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "hello.h"   // now we include the header instead of extern

MODULE_LICENSE("GPL");

static int __init user_init(void) {
    int result = add(6, 7);
    printk(KERN_INFO "6+7=%d\n", result);
    return 0;
}

static void __exit user_exit(void) {
    printk(KERN_INFO "User module unloaded\n");
}

module_init(user_init);
module_exit(user_exit);
