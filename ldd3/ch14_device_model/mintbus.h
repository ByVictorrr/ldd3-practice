#ifndef MINTBUS_H
#define MINTBUS_H
#include <linux/device.h>
struct bus_type mintbus_bus;

struct mint_id {
    const char *name;
};

struct mint_dev
{
    struct mint_id *id;
    struct device device;
    void *priv_data;

};
struct mint_driver {
    char *name;
    const struct mint_id *id_table;
    int (*probe)(struct mint_dev *);
    void (*remove)(struct mint_dev *);
    struct device_driver driver;

};
static inline struct mint_dev *to_mint_device(struct device *d)
{
    return container_of(d, struct mint_dev, device);
}

static inline struct mint_driver *to_mint_driver(struct device_driver *drv)
{
    return container_of(drv, struct mint_driver, driver);
}

static int mint_core_probe(struct device *dev)
{
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (!mdrv->probe)
        return 0;

    return mdrv->probe(mdev);
}
static void mint_core_remove(struct device *dev)
{
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (mdrv->remove)
        mdrv->remove(mdev);
}

inline static int mint_register_driver(struct mint_driver *drv)
{
    drv->driver.bus = &mintbus_bus;
    drv->driver.name = drv->name;
    drv->driver.owner = THIS_MODULE;
    drv->driver.probe = mint_core_probe;
    return driver_register(&drv->driver);
}
static inline void mint_unregister_driver(struct mint_driver *drv)
{
    driver_unregister(&drv->driver);
}

#endif