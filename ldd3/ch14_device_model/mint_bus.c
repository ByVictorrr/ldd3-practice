#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "mint.h"


static struct device *mint_root;
struct class *mint_class;
EXPORT_SYMBOL_GPL(mint_class);

int mint_core_probe(struct device *dev)
{
    /**
    really_probe(dev, drv)
    └─ if (dev->bus->probe)    // bus wrapper exists?
            dev->bus->probe(dev)  // <-- bus first
       else if (drv->probe)
            drv->probe(dev)       // direct (no bus wrapper)
    */
    // usually calls drv->probe(dev, id)
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (!mdrv->probe)
        return 0;

   dev_info(dev, "MintBus device found\n");
    return mdrv->probe(mdev);
}
void mint_core_remove(struct device *dev)
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
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (mdrv->remove)
        mdrv->remove(mdev);
    dev_info(dev, "MintBus device removed\n");
}
int _mint_core_remove(struct device *dev)
{
    mint_core_remove(dev);
    return 0;

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
    add_uevent_var(env, "SUBSYSTEM=mint_bus");
    add_uevent_var(env, "DEV_NAME=%s", dev_name(dev));
    // add_uevent_var(env, "SUBSYSTEM=mint");
    return 0;
}

static void mint_release(struct device *dev) {
    struct mint_dev *mdev = to_mint_device(dev);
    if (mdev->priv_data)
        kfree(mdev->priv_data);
    kfree(mdev);
    pr_info("mintbus: release %s\n", dev_name(dev));
}


/* ---------- sysfs attributes on the bus (add/remove devices) ---------- */
static ssize_t add_device_store(const struct bus_type *bus,
                                const char *buf, size_t count)
{
    struct mint_dev *mdev;
    char name[MAX_MINT_ID_LEN];
    size_t len;
    int ret;

    len = strcspn(buf, "\n");
    if (len == 0 || len >= sizeof(name))
        return -EINVAL;

    memcpy(name, buf, len);
    name[len] = '\0';

    /* optional: check if already exists */
    if (bus_find_device_by_name((struct bus_type *)bus, NULL, name)) {
        pr_info("mint: device %s already exists\n", name);
        return -EEXIST;
    }

    mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
    if (!mdev)
        return -ENOMEM;

    device_initialize(&mdev->device);
    // mdev->device.class   = mint_class;
    mdev->device.bus     = (struct bus_type *)bus;
    mdev->device.parent  = mint_root;
    mdev->device.release = mint_release;   /* simple: free the struct on last put */
    mdev->priv_data = NULL;

    dev_set_name(&mdev->device, "%s", name);
    strscpy(mdev->id.name, name, sizeof(mdev->id.name));

    ret = device_add(&mdev->device);
    if (ret) {
        put_device(&mdev->device);
        return ret;
    }
    mdev->class_dev = device_create(mint_class,
                                &mdev->device,  /* parent */
                                0,              /* devt, or something meaningful */
                                mdev,           /* drvdata */
                                "mint_%s", name);

    return count;
}

BUS_ATTR_WO(add_device);

static ssize_t remove_device_store(const struct bus_type *bus,
                                   const char *buf, size_t count)
{
    char name[MAX_MINT_ID_LEN];
    size_t len;
    struct device *dev;

    len = strcspn(buf, "\n");
    if (len == 0 || len >= sizeof(name))
        return -EINVAL;

    memcpy(name, buf, len);
    name[len] = '\0';

    dev = bus_find_device_by_name((struct bus_type *)bus, NULL, name);
    if (!dev)
        return -ENODEV;

    struct mint_dev *mdev = to_mint_device(dev);
    device_unregister(mdev->class_dev);
    device_unregister(dev);
    return count;
}
BUS_ATTR_WO(remove_device);

static struct attribute *mintbus_attrs[] = {
    &bus_attr_add_device.attr,
    &bus_attr_remove_device.attr,
    NULL,
};

ATTRIBUTE_GROUPS(mintbus);







struct bus_type mint_bus = {
    .name = "mint",
    .probe = mint_core_probe,
    .remove = mint_core_remove,
    .match = minibus_match,
    .uevent = minibus_uevent,
    .bus_groups = mintbus_groups,
    /** Attributes for when device_add - devices/<dev>/<attr>  folder **/
    // .dev_groups =
    /** Attributes for when driver_add - drivers/<drv>/<attr> folder **/
    // .drv_groups =
};

EXPORT_SYMBOL_GPL(mint_bus);

int mint_register_driver(struct mint_driver *drv)
{
    drv->driver.bus = &mint_bus;
    drv->driver.name = drv->name;
    drv->driver.owner = THIS_MODULE;
    // drv->driver.dev_groups == drv->driver.groups # called after probe successful for the device
    /* Deprecated; use bus->{remove,probe}
    drv->driver.probe = mint_core_probe;
    drv->driver.remove = _mint_core_remove;
    */
    return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(mint_register_driver);
void mint_unregister_driver(struct mint_driver *drv)
{
    driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(mint_unregister_driver);

static int __init mintbus_init(void)
{
    // list of drivers and devices attached to this bus
    // struct subsys_private *priv = mintbus_bus.p;
    // create /sys/devices/mintroot
    mint_root = root_device_register("mintroot");
    if (IS_ERR(mint_root))
        return PTR_ERR(mint_root);
    mint_class = class_create("mint");
    if (!mint_class) return -ENOMEM;

    return bus_register(&mint_bus);
}
static void __exit mintbus_exit(void)
{
    mint_class = class_create("mint");
    bus_unregister(&mint_bus);
    root_device_unregister(mint_root);
}

module_init(mintbus_init);
module_exit(mintbus_exit);

MODULE_DESCRIPTION("Mint bus core");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
