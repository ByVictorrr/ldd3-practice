#include "mint.h"

int mint_core_probe(struct device *dev)
{
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (!mdrv->probe)
        return 0;

    return mdrv->probe(mdev);
}
int mint_core_remove(struct device *dev)
{
    struct mint_dev *mdev = to_mint_device(dev);
    struct mint_driver *mdrv = to_mint_driver(dev->driver);

    if (mdrv->remove)
        mdrv->remove(mdev);
    return 0;
}

int mint_register_driver(struct mint_driver *drv)
{
    drv->driver.bus = &mint_bus;
    drv->driver.name = drv->name;
    drv->driver.owner = THIS_MODULE;
    drv->driver.probe = mint_core_probe;
    drv->driver.remove = mint_core_remove;
    return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(mint_register_driver);
void mint_unregister_driver(struct mint_driver *drv)
{
    driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(mint_unregister_driver);