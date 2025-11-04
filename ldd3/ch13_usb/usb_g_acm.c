#include <linux/usb/composite.h>
#include "drivers/usb/gadget/function/f_ecm.h"


static struct usb_function_instance *fi_comm;
static struct usb_function *f_comm;
static struct usb_configuration ecm_cfg = {
    .label        = "CDC ECM",
    .bmAttributes = USB_CONFIG_ATT_SELFPOWER,
    .MaxPower     = 2,       /* set appropriately */
};
static int g_ecm_bind(struct usb_composite_dev *cdev)
{
    int ret;
    /* create configuration */
    ret = usb_add_config_only(cdev, &ecm_cfg);;
    if (ret) return ret;
    /* Allocate a ethernet function */
    fi_comm = usb_get_function_instance("ecm");
    if (!fi_comm) return -ENOMEM;
    /* --- minimal fix: set distinct, LAA, unicast MACs on the instance --- */
    struct f_ecm_opts *opts =
        container_of(fi_comm, struct f_ecm_opts, func_inst);
    const u8 dev[ETH_ALEN]  = {0x02,0x12,0x34,0x56,0x78,0x9a};
    const u8 host[ETH_ALEN] = {0x06,0x12,0x34,0x56,0x78,0x9a};
    ether_addr_copy(opts->dev_addr,  dev);
    ether_addr_copy(opts->host_addr, host);
    opts->ethaddr_valid  = true;
    opts->hostaddr_valid = true;
    /* create an actual function */
    f_comm = usb_get_function(fi_comm);
    if (IS_ERR(f_comm))
    {
        usb_put_function_instance(fi_comm);
        return PTR_ERR(f_comm);
    }
    /* Add function to configuration */
    ret = usb_add_function(&ecm_cfg, f_comm);
    if (ret)
    {
        usb_put_function_instance(fi_comm);
        usb_put_function(f_comm);
        return ret;
    }
    return 0;
}
static struct usb_string strings_en[] = {
    { .id = 1, .s = "Manufacturer Name" },
    { .id = 2, .s = "Product Name" },
    { .id = 3, .s = "Serial Number" },
    { } /* Terminating entry */
};

static struct usb_gadget_strings stringtab_en = {
    .language = 0x0409, /* en-US */
    .strings = strings_en,
};
struct usb_composite_driver g_ecm_driver = {
    .name = "g_ecm",
    .dev = &(struct  usb_device_descriptor){
        .bLength = sizeof(struct usb_device_descriptor),
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = cpu_to_le16(0x0200),
        .idVendor = cpu_to_le16(0x1d6b),
        .idProduct = cpu_to_le16(0x0104),
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
    },
    .strings = (struct usb_gadget_strings *[]){&stringtab_en, NULL},
    .bind = g_ecm_bind,
};

static int __init g_ecm_init(void)
{
    return usb_composite_probe(&g_ecm_driver);
}
static void __exit g_ecm_exit(void)
{
    usb_composite_unregister(&g_ecm_driver);
}
module_init(g_ecm_init);
module_exit(g_ecm_exit);
MODULE_LICENSE("GPL");