// SPDX-License-Identifier: GPL-2.0
#include <linux/usb/composite.h>

/* ---------- Device strings ---------- */
enum { STR_MANUF, STR_PRODUCT, STR_SERIAL };
static struct usb_string dev_strings[] = {
    [STR_MANUF]   = { .s = "Manufacturer Name" },
    [STR_PRODUCT] = { .s = "Product Name" },
    [STR_SERIAL]  = { .s = "Serial Number" },
    { } /* end */
};
static struct usb_gadget_strings dev_stringtab = {
    .language = 0x0409,
    .strings  = dev_strings,
};
/* ---------- Config strings ---------- */
static struct usb_string cfg_strings[] = {
    { .s = "Conf 1" },
    { } /* end */
};
static struct usb_gadget_strings cfg_stringtab = {
    .language = 0x0409,
    .strings  = cfg_strings,
};
static struct usb_gadget_strings *cfg_stringtabs[] = {
    &cfg_stringtab, NULL,
};

/* ---------- Device descriptor ---------- */
static struct usb_device_descriptor g_acm_dev = {
    .bLength            = sizeof(struct usb_device_descriptor),
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = cpu_to_le16(0x0200),
    .bDeviceClass       = USB_CLASS_PER_INTERFACE,
    .bMaxPacketSize0    = 64,
    .bcdDevice          = cpu_to_le16(0x0100),
    .idVendor           = cpu_to_le16(0x1d6b),
    .idProduct          = cpu_to_le16(0x0104),
    .bNumConfigurations = 1,
    /* i* set in bind() */
};

/* ---------- Add ACM function into a configuration ---------- */
static int acm_do_config(struct usb_configuration *c)
{
    int ret;
    struct usb_function_instance *fi;
    struct usb_function *f;

    fi = usb_get_function_instance("acm");
    if (IS_ERR(fi))
        return PTR_ERR(fi);

    f = usb_get_function(fi);
    if (IS_ERR(f)) {
        ret = PTR_ERR(f);
        usb_put_function_instance(fi);
        return ret;
    }

    ret = usb_add_function(c, f);
    if (ret) {
        /* No ownership taken on failure */
        usb_put_function(f);
        usb_put_function_instance(fi);
    }
    dev_info(&c->cdev->gadget->dev, "acm: add_function -> %d\n", ret);
    return ret;
}

/* ---------- Composite bind ---------- */
static int g_acm_bind(struct usb_composite_dev *cdev)
{
    int ret;
    struct usb_configuration *cfg;
    enum usb_device_speed old;

    usb_ep_autoconfig_reset(cdev->gadget);

    /* Device string IDs */
    ret = usb_string_ids_tab(cdev, dev_strings);
    if (ret < 0)
        return ret;
    g_acm_dev.iManufacturer = dev_strings[STR_MANUF].id;
    g_acm_dev.iProduct      = dev_strings[STR_PRODUCT].id;
    g_acm_dev.iSerialNumber = dev_strings[STR_SERIAL].id;

    /* Config string IDs */
    ret = usb_string_ids_tab(cdev, cfg_strings);
    if (ret < 0)
        return ret;

    /* Per-bind configuration (device-managed => auto-freed) */
    cfg = devm_kzalloc(&cdev->gadget->dev, sizeof(*cfg), GFP_KERNEL);
    if (!cfg)
        return -ENOMEM;

    cfg->label               = "CDC ACM";
    cfg->bmAttributes        = USB_CONFIG_ATT_ONE; /* bit7 set (bus-powered) */
    cfg->MaxPower            = 250;                /* 500 mA in 2 mA units */
    cfg->bConfigurationValue = 1;                  /* non-zero is required */
    cfg->iConfiguration      = cfg_strings[0].id;
    cfg->strings             = cfg_stringtabs;

    dev_info(&cdev->gadget->dev,
        "g_acm: cfg attrs=0x%02x MaxPower=%u bCfgVal=%u iCfg=%d\n",
        cfg->bmAttributes, cfg->MaxPower, cfg->bConfigurationValue, cfg->iConfiguration);

    /* Force Full-Speed during add_config to avoid dummy_hcd -EINVAL */
    old = cdev->gadget->max_speed;
    cdev->gadget->max_speed = USB_SPEED_FULL;
    ret = usb_add_config(cdev, cfg, acm_do_config);
    cdev->gadget->max_speed = old;

    dev_info(&cdev->gadget->dev, "g_acm: add_config ret=%d\n", ret);
    return ret;
}

/* ---------- Driver boilerplate ---------- */
static struct usb_gadget_strings *g_acm_strings[] = { &dev_stringtab, NULL };

static struct usb_composite_driver g_acm_driver = {
    .name      = "g_acm",
    .dev       = &g_acm_dev,
    .strings   = g_acm_strings,
    .bind      = g_acm_bind,
    .max_speed = USB_SPEED_FULL,  /* safest on dummy_hcd/QEMU */
};

static int __init g_acm_init(void)
{
    return usb_composite_probe(&g_acm_driver);
}
static void __exit g_acm_exit(void)
{
    usb_composite_unregister(&g_acm_driver);
}
module_init(g_acm_init);
module_exit(g_acm_exit);
MODULE_LICENSE("GPL");
