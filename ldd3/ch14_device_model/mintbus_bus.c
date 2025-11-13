#include <mintbus.h>
#include <linux/device.h>


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
        if (!strcmp(id->name, mdev->id->name))
            return 1;
    return 0;
}

static int minibus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    // Add bus specific uevent env vars
    // for pci it would be the pci.ids
    // for KOBj_ADD its called in device_add
    struct mint_dev *mdev = to_mint_device(dev);
    add_uevent_var(env, "MODALIAS=mintbus:%s", mdev->id->name);
    add_uevent_var(env, "DEV_NAME=%s", dev_name(dev));
    return 0;
}

struct bus_type mintbus_bus = {
    .probe = mintbus_probe,
    .remove = mintbus_remove,
    .match = minibus_match,
    .uevent = minibus_uevent,
};



static int __init mintbus_init(void)
{
    // list of drivers and devices attached to this bus
    // struct subsys_private *priv = mintbus_bus.p;

    return bus_register(&mintbus_bus);
}
static void __exit mintbus_exit(void) { bus_unregister(&mintbus_bus); }

module_init(mintbus_init);
module_exit(mintbus_exit);
MODULE_LICENSE("GPL");