#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/usb.h>
#include "uac_ioctl.h"
#include "uac.h"

#define UAC_CS_INTERFACE   0x24
#define UAC_FU_DESCRIPTOR  0x06

/* Request Type */
#define BMRT_CLASS_IF_OUT  (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) // 0x21
#define BMRT_CLASS_IF_IN   (USB_DIR_IN  | USB_TYPE_CLASS | USB_RECIP_INTERFACE) // 0xA1

/* Audio class request codes */
#define UAC_SET_CUR 0x01
#define UAC_GET_CUR 0x81
#define UAC_GET_MIN 0x82
#define UAC_GET_MAX 0x83

/* Feature unit control selectors */
#define UAC_FU_MUTE   0x01
#define UAC_FU_VOLUME 0x02

static inline __u16 uac_wValue(u8 cs, u8 ch)       { return (cs << 8) | ch; }
static inline __u16 uac_wIndex(u8 fu_id, u8 ifnum) { return (fu_id << 8) | ifnum; }

int uac_parse_feature_unit(struct uac_dev * dev)
{

    const struct usb_interface *intf = dev->audio_ctrl;
    const unsigned char *p = intf->cur_altsetting->extra; // get extra descriptors
    int len = intf->cur_altsetting->extralen;
    dev->ac_ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
    dev_info(&intf->dev, "Control Interface Number=%u\n", dev->ac_ifnum);
    while (len > 8)
    {
        /**
        * Offset  Size  Field
            0       1     bLength            (== p[0])
            1       1     bDescriptorType    (0x24 for CS_INTERFACE; == p[1])
            2       1     bDescriptorSubType (== p[2])
            ...           (rest depends on subtype)
        */
        u8 bl = p[0], type=p[1], sub=p[2];
        if (!bl || bl > len) break;
        if (type == UAC_CS_INTERFACE && sub==UAC_FU_DESCRIPTOR)
        {
            dev->fu_id = p[3]; /* bUnitHD */
            dev_info(&intf->dev, "Feature Unit found, ID=%u\n", dev->fu_id);
            return 0;

        }
        len -= bl;
        p += bl;
    }
    dev_warn(&intf->dev, "UAC: no Feature Unit found\n");
    dev->fu_id = 0;
    return -ENOENT;
}
/* ---- MUTE ---- */

static int uac_set_mute(struct uac_dev *dev, int ch, bool on)
{
    int ret;
    u8 *b = kmalloc(1, GFP_KERNEL);
    u8 bm = BMRT_CLASS_IF_OUT;    /* 0x21 */
    u8 rq = UAC_SET_CUR;          /* 0x01 */
    u16 wV = uac_wValue(UAC_FU_MUTE, ch);
    u16 wI = uac_wIndex(dev->fu_id, dev->ac_ifnum);

    if (!b) return -ENOMEM;
    *b = on ? 1 : 0;

    ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
                          rq, bm, wV, wI, b, 1, USB_CTRL_SET_TIMEOUT);


    kfree(b);
    return (ret == 1) ? 0 : (ret < 0 ? ret : -EIO);
}

static int uac_get_mute(struct uac_dev *dev, int ch, int __user *argp)
{
    int ret;
    u8 *b = kmalloc(1, GFP_KERNEL);
    u8 bm = BMRT_CLASS_IF_IN;     /* 0xA1 */
    u8 rq = UAC_GET_CUR;          /* 0x81 */
    u16 wV = uac_wValue(UAC_FU_MUTE, ch);
    u16 wI = uac_wIndex(dev->fu_id, dev->ac_ifnum);

    if (!b) return -ENOMEM;

    ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                          rq, bm, wV, wI, b, 1, USB_CTRL_GET_TIMEOUT);


    if (ret != 1) { kfree(b); return ret < 0 ? ret : -EIO; }

    /* ioctl contract: int 0/1 */
	int v = *b ? 1 : 0;
	if (copy_to_user(argp, &v, sizeof(v))) { kfree(b); return -EFAULT; }

    dev_info(&dev->audio_ctrl->dev, "GET_MUTE byte=%u\n", *b);
    kfree(b);
    return 0;
}

/* ---- VOLUME (16-bit, 1/256 dB) ---- */

static int uac_get_vol16(struct uac_dev *dev, u8 ch, u8 req, s16 *out)
{
    int ret;
    __le16 *raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    u8  bm = BMRT_CLASS_IF_IN;         /* 0xA1 */
    u8  rq = req;                      /* 0x81/0x82/0x83 */
    u16 wV = uac_wValue(UAC_FU_VOLUME, ch);
    u16 wI = uac_wIndex(dev->fu_id, dev->ac_ifnum);

    if (!raw) return -ENOMEM;

    ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                          rq, bm, wV, wI, raw, sizeof(*raw), USB_CTRL_GET_TIMEOUT);


    if (ret != sizeof(*raw)) { kfree(raw); return ret < 0 ? ret : -EIO; }

    *out = (s16)le16_to_cpu(*raw);
    dev_info(&dev->audio_ctrl->dev, "GET_VOL16 payload(le)=0x%04x val=%d\n",
             (unsigned)le16_to_cpu(*raw), *out);
    kfree(raw);
    return 0;
}

static int uac_set_vol16(struct uac_dev *dev, u8 ch, s16 val)
{
    int ret;
    __le16 *raw = kmalloc(sizeof(*raw), GFP_KERNEL);
    u8  bm = BMRT_CLASS_IF_OUT;        /* 0x21 */
    u8  rq = UAC_SET_CUR;              /* 0x01 */
    u16 wV = uac_wValue(UAC_FU_VOLUME, ch);
    u16 wI = uac_wIndex(dev->fu_id, dev->ac_ifnum);

    if (!raw) return -ENOMEM;
    *raw = cpu_to_le16((u16)val);

    ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
                          rq, bm, wV, wI, raw, sizeof(*raw), USB_CTRL_SET_TIMEOUT);


    dev_info(&dev->audio_ctrl->dev, "SET_VOL16 payload(le)=0x%04x val=%d\n",
             (unsigned)le16_to_cpu(*raw), val);

    kfree(raw);
    return (ret == sizeof(*raw)) ? 0 : (ret < 0 ? ret : -EIO);
}
/* ---- Stereo helpers: volume lives on ch1 (L) and ch2 (R) ---- */
static int uac_set_vol_stereo(struct uac_dev *dev, s16 val)
{
	    int ret = uac_set_vol16(dev, /*ch=*/1, val);  /* Left */
	    if (ret) return ret;
	    return uac_set_vol16(dev, /*ch=*/2, val);     /* Right */
	}

static int uac_get_vol_stereo_avg(struct uac_dev *dev, s16 *out)
{
	    s16 l = 0, r = 0;
	    int ret = uac_get_vol16(dev, /*ch=*/1, UAC_GET_CUR, &l);
	    if (ret) return ret;
	    ret = uac_get_vol16(dev, /*ch=*/2, UAC_GET_CUR, &r);
	    if (ret) return ret;
	    *out = (l + r) / 2;
	    return 0;
}


long uac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct uac_dev *dev = filp->private_data;
    s16 v;
    int out, ret;
    switch (cmd) {
    case UAC_IOC_SET_MUTE:
        int in;
        if (copy_from_user(&in, (void __user *)arg, sizeof(in)))
            return -EFAULT;
        if (in != 0 && in != 1)
            return -EINVAL;
        /* Mute is channel 0 (master) only on this device */
        return uac_set_mute(dev, /*ch=*/0, in ? true : false);

    case UAC_IOC_GET_MUTE:
        return uac_get_mute(dev, 0, (int __user *)arg);

    case UAC_IOC_GET_MIN_VOL:
        /* Volume is on ch1/ch2; min is typically same for both – read Left */
        ret = uac_get_vol16(dev, /*ch=*/1, UAC_GET_MIN, &v);
        if (ret) return ret;
        out = v;
        return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
    case UAC_IOC_GET_MAX_VOL:
        ret = uac_get_vol16(dev, 1, UAC_GET_MAX, &v);
        if (ret) return ret;
        out = v;
        return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
    case UAC_IOC_GET_VOL:
        ret = uac_get_vol_stereo_avg(dev, &v);  /* average L/R */
        if (ret) return ret;
        out = v;
        return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
    case UAC_IOC_SET_VOL: {
            int in;
            if (copy_from_user(&in, (void __user *)arg, sizeof(in))) return -EFAULT;
            return uac_set_vol_stereo(dev, (s16)in);
    }
    default:
        return -EINVAL;
    }
}
