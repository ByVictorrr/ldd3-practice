#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/usb.h>
#include "uac.h"
#include "uac_ioctl.h"
enum { CH_MASTER = 0, CH_L = 1, CH_R = 2 };
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
#define UAC_GET_RES 0x84

/* Feature unit control selectors */
#define UAC_FU_MUTE   0x01
#define UAC_FU_VOLUME 0x02

#define UAC_TIMEOUT 1000
static inline __u16 uac_wValue(u8 cs, u8 ch)       { return (cs << 8) | ch; }
static inline __u16 uac_wIndex(u8 fu_id, u8 ifnum) { return (fu_id << 8) | ifnum; }


int _uac_set_volume(struct uac_dev * dev, int ch, s16 vol)
{
    int ret;
    __le16 *v = kmalloc(sizeof(*v), GFP_KERNEL);
    if (!v) return -ENOMEM;
    s32 offset = vol - dev->min_vol;
    offset = ((offset + dev->res_vol / 2) / dev->res_vol) *  dev->res_vol;  // round to nearest step
    vol = dev->min_vol + offset;
    if (vol < dev->min_vol) vol = dev->min_vol;
    if (vol > dev->max_vol) vol = dev->max_vol;
    *v = cpu_to_le16(vol);
    ret = usb_control_msg(dev->udev,
        usb_sndctrlpipe(dev->udev, 0),
        UAC_SET_CUR,
        BMRT_CLASS_IF_OUT,
        uac_wValue(UAC_FU_VOLUME, ch),
        uac_wIndex(dev->fu_id, dev->ac_ifnum),
        v,
        sizeof(*v),
        UAC_TIMEOUT
    );
    if (ret != sizeof(*v)) {
        dev_err(&dev->udev->dev, "SET_VOL(VOL ch=%d) failed: %d\n", ch, ret);
        kfree(v);
        return (ret < 0) ? ret : -EIO;
    }
    /* Optional: debug print as integer dB. Kernel can't print floats. */
    dev_dbg(&dev->udev->dev, "VOL[ch%d]=%d.%03u dB (raw=%d)\n", ch, vol / 256,
            (unsigned)((abs(vol % 256) * 1000) / 256),
            vol);
    kfree(v);
    return 0;
}
int uac_set_volume(struct uac_dev * dev, s16 vol)
{
    int ret;
    ret = _uac_set_volume(dev, CH_L, vol);
    if (ret < 0) return ret;
    ret = _uac_set_volume(dev, CH_R, vol);
    if (ret < 0) return ret;
    return ret;
}
int _uac_get_volume_param(struct uac_dev * dev, int ch, s16 *vol, int parameter)
{
    int ret;
    __le16 *v = kmalloc(sizeof(*v), GFP_KERNEL);
    if (!v) return -ENOMEM;

    ret = usb_control_msg(dev->udev,
        usb_rcvctrlpipe(dev->udev, 0),
        parameter,
        BMRT_CLASS_IF_IN,
        uac_wValue(UAC_FU_VOLUME, ch),
        uac_wIndex(dev->fu_id, dev->ac_ifnum),
        v,
        sizeof(*v),
        UAC_TIMEOUT
        );
    if (ret != sizeof(*v)) {
        dev_err(&dev->udev->dev, "%d(VOL ch=%d) failed: %d\n", parameter, ch, ret);
        kfree(v);
        return (ret < 0) ? ret : -EIO;
    }
    *vol = (s16)le16_to_cpu(*v);  /* signed Q8.8 dB */
    /* Optional: debug print as integer dB. Kernel can't print floats. */
    dev_dbg(&dev->udev->dev, "VOL[ch%d]=%d.%03u dB (raw=%d)\n", ch,
            *vol / 256,
            (unsigned)((abs(*vol % 256) * 1000) / 256),
            *vol);
    kfree(v);
    return 0;
}
int uac_get_volume(struct uac_dev * dev, s16 *vol, int parameter)
{
    int ret;
    s16 l, r;
    ret = _uac_get_volume_param(dev, CH_L, &l, parameter);
    if (ret < 0) return ret;
    ret = _uac_get_volume_param(dev, CH_R, &r, parameter);
    if (ret < 0) return ret;
    *vol = (l + r)/ 2;
    return 0;
}
int uac_get_mute(struct uac_dev * dev, bool *is_mute)
{
    int ret;
    u8 *v = kmalloc(sizeof(*v), GFP_KERNEL);
    if (!v) return -ENOMEM;
    ret = usb_control_msg(dev->udev,
        usb_rcvctrlpipe(dev->udev, 0),
        UAC_GET_CUR,
        BMRT_CLASS_IF_IN,
        uac_wValue(UAC_FU_MUTE, CH_MASTER),
        uac_wIndex(dev->fu_id, dev->ac_ifnum),
        v,
        sizeof(*v),
        UAC_TIMEOUT
        );
    if (ret != sizeof(*v)) {
        dev_err(&dev->udev->dev, "GET_MUTE failed: %d\n", ret);
        kfree(v);
        return (ret < 0) ? ret : -EIO;
    }
    *is_mute = *v == 1;
    /* Optional: debug print as integer dB. Kernel can't print floats. */
    dev_dbg(&dev->udev->dev, "IS MUTED=%d\n", *is_mute);
    kfree(v);
    return 0;
}

int uac_set_mute(struct uac_dev * dev, bool mute)
{
    int ret;
    u8 *v = kmalloc(sizeof(*v), GFP_KERNEL);
    if (!v) return -ENOMEM;

    *v = mute ? 1 : 0;
    ret = usb_control_msg(dev->udev,
        usb_sndctrlpipe(dev->udev, 0),
        UAC_SET_CUR,
        BMRT_CLASS_IF_OUT,
        uac_wValue(UAC_FU_MUTE, CH_MASTER),
        uac_wIndex(dev->fu_id, dev->ac_ifnum),
        v,
        sizeof(*v),
        UAC_TIMEOUT
        );
    if (ret != sizeof(*v)) {
        dev_err(&dev->udev->dev, "SET_MUTE failed: %d\n", ret);
        kfree(v);
        return (ret < 0) ? ret : -EIO;
    }
    /* Optional: debug print as integer dB. Kernel can't print floats. */
    dev_dbg(&dev->udev->dev, "IS MUTED=%d\n", *v);
    kfree(v);
    return 0;
}

long uac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct uac_dev *dev = filp->private_data;
    s16 v;
    bool is_muted;
    int ret;
    switch (cmd) {
    case UAC_IOC_SET_MUTE:
        if (copy_from_user(&is_muted, (void *)arg, sizeof(is_muted))) return -EFAULT;
        ret = uac_set_mute(dev, is_muted);
        if (ret < 0) return ret;
        break;
    case UAC_IOC_GET_MUTE:
        ret = uac_get_mute(dev, &is_muted);
        if (ret < 0) return ret;
        if (copy_to_user((void *)arg, &is_muted, sizeof(is_muted))) return -EFAULT;
        break;
    case UAC_IOC_GET_MIN_VOL:
        ret = uac_get_volume(dev, &v, UAC_GET_MIN);
        if (ret < 0) return ret;
        if (copy_to_user((void *)arg, &v, sizeof(v))) return -EFAULT;
        break;
    case UAC_IOC_GET_MAX_VOL:
        ret = uac_get_volume(dev, &v, UAC_GET_MAX);
        if (ret < 0) return ret;
        if (copy_to_user((void *)arg, &v, sizeof(v))) return -EFAULT;
        break;
    case UAC_IOC_GET_RES_VOL:
        ret = uac_get_volume(dev, &v, UAC_GET_RES);
        if (ret < 0) return ret;
        if (copy_to_user((void *)arg, &v, sizeof(v))) return -EFAULT;
        break;
    case UAC_IOC_GET_VOL:
        ret = uac_get_volume(dev, &v, UAC_GET_CUR);
        if (ret < 0) return ret;
        if (copy_to_user((void *)arg, &v, sizeof(v))) return -EFAULT;
        break;
    case UAC_IOC_SET_VOL:
        if (copy_from_user(&v, (void *)arg, sizeof(v))) return -EFAULT;
        if (v < dev->min_vol || v > dev->max_vol) return -EINVAL;
        ret = uac_set_volume(dev, v);
        if (ret < 0) return ret;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}
int uac_parse_feature_unit(struct uac_dev * dev)
{
    struct usb_interface * intf = dev->audio_ctrl;
    // step 1 - get ac interface
    dev->ac_ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
    // step 2 - iterate around the extra descriptors
    int len = intf->cur_altsetting->extralen, ret;
    s16 v;
    unsigned char *ptr = intf->cur_altsetting->extra;
    while (len > 3)
    {
        u8 d_len = ptr[0], d_type = ptr[1], d_sub_type = ptr[2];
        if (!d_len || len < d_len) return -EINVAL;

        if (d_type == USB_DT_CS_INTERFACE && d_sub_type == UAC_FU_DESCRIPTOR)
        {
            // found descriptor
            dev->fu_id = ptr[3]; // bUnitId or FU ID
            dev_info(&dev->udev->dev, "found FU ID %d\n", (int)dev->fu_id);
            /* Here I could parse the bmaControls at ptr[5] each one of those are ptr[4] bytes*/
            /* Here get the max and min volumes*/
            ret = uac_get_volume(dev, &v, UAC_GET_MIN);
            if (ret < 0) return ret;
            dev->min_vol = v;
            ret = uac_get_volume(dev, &v, UAC_GET_MAX);
            if (ret < 0) return ret;
            dev->max_vol = v;
            ret = uac_get_volume(dev, &v, UAC_GET_RES);
            if (ret < 0) return ret;
            dev->res_vol = v;
            return 0;
        }
        len-=d_len;
        ptr+=d_len;
    }
    return -ENODEV;
}

