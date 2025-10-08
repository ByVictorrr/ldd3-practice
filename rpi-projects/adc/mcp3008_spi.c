#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/gpio.h>


static int howmany = 1;
static char *whom = "world";

module_param(howmany, int, 0444);
MODULE_PARM_DESC(howmany, "Number of greetings");

module_param(whom, charp, 0444);
MODULE_PARM_DESC(whom, "Whom to greet");

static int __init hello_init(void) {
    int i;
    for (i = 0; i < howmany; i++)
        printk(KERN_INFO "Hello, %s!\n", whom);
    return 0;
}

static void __exit hello_exit(void) {
    printk(KERN_INFO "Goodbye, %s!\n", whom);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Module with parameters");
MODULE_VERSION("0.2");