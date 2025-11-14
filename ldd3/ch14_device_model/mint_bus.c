#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "mint.h"


static struct device *mint_root;


static int mintbus_probe(struct device *dev)
{
    /**
        really_probe(dev, drv)
        └─ if (dev->bus->probe)    // bus wrapper exists?
                dev->bus->probe(dev)  // <-- bus first
           else if (drv->probe)
                drv->probe(dev)       // direct (no bus wrapper)
     */
   // usually calls drv->probe(dev, id)
   dev_info(dev, "MintBus device found\n");
   return mint_core_probe(dev);
}
static void mintbus_remove(struct device *dev)
{

    /**
    __device_release_driver(dev)
    └─ if (dev->bus->remove)
        dev->bus->remove(dev)     // bus wrapper first
        └─ drv->remove(dev)       // calls driver’s remove
       else if (drv->remove)
        drv->remove(dev)          // direct fallback

     */
    // usually calls drv->remove(dev)
    dev_info(dev, "MintBus device removed\n");
    mint_core_remove(dev);
}
static int minibus_match(struct device *dev, const struct device_driver *drv)
{
    // this gets called when the core finds a new device and tries to match a device driver
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(drv);
    const struct mint_id *id;
    for (id = mdrv->id_table; id->name; id++)
        if (!strcmp(id->name, mdev->id.name))
            return 1;
    return 0;
}

static int minibus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    // Add bus specific uevent env vars for device events
    // for pci it would be the pci.ids
    // for KOBj_ADD its called in device_add
    struct mint_dev *mdev = to_mint_device(dev);
    add_uevent_var(env, "MODALIAS=mintbus:name=%s", mdev->id.name);
    add_uevent_var(env, "DEV_NAME=%s", dev_name(dev));
    return 0;
}

static void mint_release(struct device *dev) {
    struct mint_dev *mdev = to_mint_device(dev);
    kfree(mdev);
    pr_info("mintbus: release %s\n", dev_name(dev));
}


struct bus_type mint_bus;
static ssize_t add_device_store(const struct bus_type *bus, const char *buf, size_t count)
{
    char name[MAX_MINT_ID_LEN];
    size_t len;
    int ret;
    struct mint_dev *mdev ;

    /* parse buf, create your mint_device, call device_register/device_add */
    pr_info("mintbus: add_device_store(): got '%.*s'\n", (int)count, buf);
    /* Strip trailing newline and limit length */
    len = strcspn(buf, "\n");
    if (len == 0 || len >= sizeof(name))
        return -EINVAL;

    memcpy(name, buf, len);
    name[len] = '\0';
    /* Optional: check if device already exists */
    if (bus_find_device_by_name((struct bus_type *)bus, NULL, buf)) {
        pr_info("mintbus: device %s already exists\n", buf);
        return -EEXIST;
    }
    mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
    if (!mdev) return -ENOMEM;
    strscpy(mdev->id.name, name, sizeof(mdev->id.name));
    device_initialize(&mdev->device);                    // init dev + kobject
    mdev->device.bus = &mint_bus;                     // attach to our bus
    mdev->device.parent = mint_root;
    // mdev->device.class = &mintbus_bus;                     // attach to our bus
    mdev->device.release = mint_release;                 // required!
    dev_set_name(&mdev->device, name);                // device name
    ret = device_add(&mdev->device);                    // adds to sysfs, emits uevent
    if (ret)
    {
        put_device(&mdev->device); // ref count 0
        return ret;

    }
    return count;
}
BUS_ATTR_WO(add_device); // creates a var and looks for add_device{_store}



static struct attribute *mintbus_attrs[] = {
    &bus_attr_add_device.attr,
    NULL
};
static const struct attribute_group mintbus_attr_group = {
    .attrs = mintbus_attrs,

};
static const struct attribute_group *mintbus_groups[] = {
    &mintbus_attr_group,
    NULL,

};


struct bus_type mint_bus = {
    .name = "mint",
    .probe = mintbus_probe,
    .remove = mintbus_remove,
    .match = minibus_match,
    .uevent = minibus_uevent,
    .bus_groups = mintbus_groups,
};

EXPORT_SYMBOL_GPL(mint_bus);

static int __init mintbus_init(void)
{
    // list of drivers and devices attached to this bus
    // struct subsys_private *priv = mintbus_bus.p;
    // create /sys/devices/mintroot
    mint_root = root_device_register("mintroot");
    if (IS_ERR(mint_root))
        return PTR_ERR(mint_root);

    return bus_register(&mint_bus);
}
static void __exit mintbus_exit(void)
{
    bus_unregister(&mint_bus);
    root_device_unregister(mint_root);
}

module_init(mintbus_init);
module_exit(mintbus_exit);
MODULE_LICENSE("GPL");
