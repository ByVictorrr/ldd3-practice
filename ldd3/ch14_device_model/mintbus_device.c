#include <linux/device.h>
#include "mintbus.h"

static struct mint_dev mint0 = {

};


static void mint_release(struct device *dev) {
    pr_info("mintbus: release %s\n", dev_name(dev));
}


static int __init mintdev_init(void) {
    device_initialize(&mint0);                    // init dev + kobject
    mint0.bus = &mintbus_bus;                     // attach to our bus
    mint0.class = &mintclass;
    mint0.release = mint_release;                 // required!
    dev_set_name(&mint0, "mint0");                // device name
    return device_add(&mint0);                    // adds to sysfs, emits uevent
}

static void __exit mintdev_exit(void) {
    device_del(&mint0);
    put_device(&mint0);                           // balances device_initialize
}
module_init(mintdev_init);
module_exit(mintdev_exit);
MODULE_LICENSE("GPL");
