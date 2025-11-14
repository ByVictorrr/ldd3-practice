#include <linux/device.h>
#include "mint.h"



static int probe(struct mint_dev *dev)
{
    dev_info(&dev->device, "driver_probe: added mint device\n");
    return 0;
}

static void remove(struct mint_dev *dev)
{
    dev_info(&dev->device, "driver_remove: remove mint device\n");
}
static struct mint_id mint_id_table[] = {
    {.name = "peppermint"},
    {.name = "spearmint"},
    {.name = ""},
};
struct mint_driver mint_drv = {
    .name = "mint_driver",
    .probe = probe,
    .remove = remove,
    .id_table = mint_id_table,
};
MODULE_DEVICE_TABLE(mint, mint_id_table);

static int __init mintdrv_init(void){ return mint_register_driver(&mint_drv); }
static void __exit mintdrv_exit(void){ mint_unregister_driver(&mint_drv); }
module_init(mintdrv_init);
module_exit(mintdrv_exit);
MODULE_DESCRIPTION("Mint bus sample driver");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_LICENSE("GPL");