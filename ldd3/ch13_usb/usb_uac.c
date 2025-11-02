#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/dma-mapping.h>
#include "uac_ioctl.h"


#define DRIVER_NAME "uac"

#define AUDIO_CONTROL_INTERFACE 0
#define AUDIO_STREAMING_INTERFACE 1
#define AUDIO_STREAMING_ALT_SETTING_STREAM_OFF 0
#define AUDIO_STREAMING_ALT_SETTING_STREAM_on 1

#define BUF_SIZE 192
#define NUM_OF_URBS 8
#define NUM_OF_PACKETS 16
#define AUDIO_RING_SIZE 64*1024
#define USB_AUDIO_VENDOR_ID    0x46f4
#define USB_AUDIO_PRODUCT_ID     0x0002
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

static struct usb_driver usb_audio_drv;

struct uac_dev
{
	struct usb_device *udev;

	struct usb_interface *audio_ctrl;
	struct usb_interface *audio_stream;

	spinlock_t lock;
	u8 ep_out; // ISO OUT endpoint <bit 7 = direction>; bit 0-3= endpoint # (0-15)>
	u8 ac_ifnum;
	u8 fu_id;
	struct urb *urbs[NUM_OF_URBS];
	struct kfifo audio_ring;
	struct wait_queue_head wait_queue;
	bool shutting_down;

};
static void uac_fill_iso_frames(struct uac_dev *dev, struct urb *urb)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	for (int j = 0; j < urb->number_of_packets; j++) {
		u8 *dst = urb->transfer_buffer + urb->iso_frame_desc[j].offset;
		int got = kfifo_out(&dev->audio_ring, dst, BUF_SIZE);
		if (got < BUF_SIZE) {
			memset(dst + got, 0, BUF_SIZE - got);  // pad
		}
		urb->iso_frame_desc[j].length = BUF_SIZE;
		/* Tee to read-ring so user can read back what we sent */
		kfifo_in(&dev->audio_ring, dst, BUF_SIZE);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->wait_queue); // wake readers/writers
}
static ssize_t uac_write(struct file *filp, const char __user *buf,
						 size_t count, loff_t *ppos)
{
	struct uac_dev *dev = filp->private_data;
	char tmp[BUF_SIZE];
	size_t off = 0;

	while (off < count) {
		size_t chunk = min(count - off, sizeof(tmp));
		if (copy_from_user(tmp, buf + off, chunk))
			return off ? off : -EFAULT;

		spin_lock_bh(&dev->lock);
		chunk = kfifo_in(&dev->audio_ring, tmp, chunk);
		spin_unlock_bh(&dev->lock);

		if (!chunk) {
			/* wait for space; your existing wait_queue is fine */
			if (wait_event_interruptible(dev->wait_queue,
					kfifo_avail(&dev->audio_ring) >= 192))
				return off ? off : -ERESTARTSYS;
			continue;
		}
		off += chunk;
		wake_up_interruptible(&dev->wait_queue);
	}
	return off;
}

static ssize_t uac_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct uac_dev *dev = filp->private_data;
	char * ring_buf = kzalloc(AUDIO_RING_SIZE, GFP_KERNEL);
	size_t n;
	if (kfifo_is_empty(&dev->audio_ring))
	{
		if (wait_event_interruptible(dev->wait_queue, !kfifo_is_empty(&dev->audio_ring))) return -ERESTARTSYS;
	}
	spin_lock_bh(&dev->lock);
	count = min(kfifo_len(&dev->audio_ring), count);
	n = kfifo_out(&dev->audio_ring, ring_buf, count);
	count = min(n, count);
	// pr_info("uac: reading %d bytes\n", (int)count);
	spin_unlock_bh(&dev->lock);
	if (copy_to_user(buf, ring_buf, count))
	{
		pr_err("uac: copy_to_user failed\n");
		count = -EFAULT;
	}
	kfree(ring_buf);
	return count;
}

static int uac_open(struct inode *inode, struct file *filp)
{
	struct usb_interface * intf;
	struct uac_dev * dev;

	intf = usb_find_interface(&usb_audio_drv, iminor(inode));
	if (!intf)
	{
		pr_err("uac: failed to find interface\n");
		return -ENODEV;                   /* minor not bound */

	}
	dev = usb_get_intfdata(intf);
	if (!dev)
	{

		pr_err("uac: failed to find intfdata\n");
		return -ENODEV;                   /* minor not bound */
	}
	filp->private_data = dev;
	return 0;
}
static int uac_release(struct inode *inode, struct file *filp)
{
	return 0;
}


/* constants per your lsusb: FU=2, AC IF=0, Mute only on ch=0 */
static int uac_set_mute(struct uac_dev *dev, bool on)
{
	u8 b = on ? 1 : 0;
	int ret = usb_control_msg(dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		UAC_SET_CUR,               /* 0x01 */
		BMRT_CLASS_IF_OUT,         /* 0x21 */
		uac_wValue(UAC_FU_MUTE, 0),/* 0x0100 */
		uac_wIndex(2, 0),          /* 0x0200 */
		&b, 1, 1000);
	dev_info(&dev->audio_ctrl->dev, "SET_MUTE ret=%d\n", ret);
	return (ret == 1) ? 0 : (ret < 0 ? ret : -EIO);
}
static int uac_get_mute(struct uac_dev *dev, int __user *argp)
{
	u8 b = 0;
	int ret = usb_control_msg(dev->udev,
		usb_rcvctrlpipe(dev->udev, 0),
		UAC_GET_CUR,               /* 0x81 */
		BMRT_CLASS_IF_IN,          /* 0xA1 */
		uac_wValue(UAC_FU_MUTE, 0),/* 0x0100 */
		uac_wIndex(2, 0),          /* 0x0200 */
		&b, 1, 1000);
	if (ret != 1) {
		dev_warn(&dev->audio_ctrl->dev, "GET_MUTE ret=%d\n", ret);
		return ret < 0 ? ret : -EIO;
	}
	{
		int v = b ? 1 : 0; /* ioctl contract: int */
		if (copy_to_user(argp, &v, sizeof(v)))
			return -EFAULT;
	}
	dev_info(&dev->audio_ctrl->dev, "get mute byte=%u\n", b);
	return 0;
}

static int uac_get_vol16(struct uac_dev *dev, u8 ch, u8 req, s16 *out)
{
	__le16 raw = 0;
	int ret = usb_control_msg(dev->udev,
		usb_rcvctrlpipe(dev->udev, 0),
		req,                                // UAC_GET_MIN/MAX/CUR (0x82/0x83/0x81)
		BMRT_CLASS_IF_IN,                   // 0xA1
		uac_wValue(UAC_FU_VOLUME, ch),      // (0x02<<8)|ch  (ch=1 or 2)
		uac_wIndex(dev->fu_id, dev->ac_ifnum), // (FU<<8)|AC_IF (2<<8)|0 for your device)
		&raw, sizeof(raw),
		1000);
	if (ret != sizeof(raw))
		return ret < 0 ? ret : -EIO;

	*out = (s16)le16_to_cpu(raw);
	return 0;
}

static int uac_set_vol16(struct uac_dev *dev, u8 ch, s16 val)
{
	__le16 raw = cpu_to_le16((u16)val);
	int ret = usb_control_msg(dev->udev,
		usb_sndctrlpipe(dev->udev, 0),
		UAC_SET_CUR,                        // 0x01
		BMRT_CLASS_IF_OUT,                  // 0x21
		uac_wValue(UAC_FU_VOLUME, ch),
		uac_wIndex(dev->fu_id, dev->ac_ifnum),
		&raw, sizeof(raw),
		1000);
	return (ret == sizeof(raw)) ? 0 : (ret < 0 ? ret : -EIO);
}

static long uac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct uac_dev *dev = filp->private_data;
	s16 v;
	int out, ret;
	switch (cmd) {
	case UAC_IOC_SET_MUTE:
		return uac_set_mute(dev, true);

	case UAC_IOC_CLR_MUTE:
		return uac_set_mute(dev, false);

	case UAC_IOC_GET_MUTE:
		return uac_get_mute(dev, (int __user *)arg);

	case UAC_IOC_GET_MIN_VOL:
			ret = uac_get_vol16(dev, 1, UAC_GET_MIN, &v);
			if (ret) return ret;
			out = v;
			return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
	case UAC_IOC_GET_MAX_VOL:
			ret = uac_get_vol16(dev, 1, UAC_GET_MAX, &v);
			if (ret) return ret;
			out = v;
			return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
	case UAC_IOC_GET_VOL:
			ret = uac_get_vol16(dev, 1, UAC_GET_CUR, &v);
			if (ret) return ret;
			out = v;
			return copy_to_user((int __user *)arg, &out, sizeof(out)) ? -EFAULT : 0;
	case UAC_IOC_SET_VOL: {
			int in;
			if (copy_from_user(&in, (void __user *)arg, sizeof(in))) return -EFAULT;
			return uac_set_vol16(dev, 1, (s16)in);
	}
	default:
		return -EINVAL;
	}
}


static const struct file_operations uac_fops = {
	.owner  = THIS_MODULE,
	.open   = uac_open,
	.read   = uac_read,
	.write  = uac_write,
	.unlocked_ioctl = uac_ioctl,
	.release = uac_release,
	.llseek = noop_llseek,
};
static struct usb_class_driver uac_class = {
	.name  = "uac%d",
	.fops  = &uac_fops,
	.minor_base = 0,
};

static void uac_callback(struct urb *urb)
{
	struct uac_dev *dev = urb->context;

	/* If teardown is in progress, or this URB is being killed, just exit */
	if (dev->shutting_down ||
		urb->status == -ENOENT ||        /* killed */
		urb->status == -ECONNRESET ||    /* unlinked */
		urb->status == -ESHUTDOWN)       /* ep/device disabled */
			return;

	if (urb->status)
		dev_err(&dev->udev->dev, "uac: urb status %d\n", urb->status);
	uac_fill_iso_frames(dev, urb);
	if (!dev->shutting_down)
	{
		usb_submit_urb(urb, GFP_ATOMIC);

	}
}


/* Find first isoschronous OUT endpoint on the current altsetting */
static int uac_find_iso_out_ep(struct usb_interface *intf, u8 *ept_addr)
{
	for (int i =0; i < intf->cur_altsetting->desc.bNumEndpoints; i++)
	{
		const struct usb_endpoint_descriptor *desc = &intf->cur_altsetting->endpoint[i].desc;
		if (usb_endpoint_xfer_isoc(desc) && usb_endpoint_dir_out(desc))
		{
			*ept_addr = desc->bEndpointAddress;
			return 0;
		}
	}
	return -ENODEV;
}
#define UAC_CS_INTERFACE   0x24
#define UAC_FU_DESCRIPTOR  0x06
static int uac_parse_feature_unit(struct uac_dev * dev)
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
static int uac_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int ret;

	struct usb_interface *stream_intf = NULL;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct uac_dev * dev = devm_kzalloc(&udev->dev, sizeof(*dev), GFP_KERNEL);
	dev->udev = udev;
	if (intf->cur_altsetting->desc.bInterfaceNumber == AUDIO_STREAMING_INTERFACE)
	{
		stream_intf = intf;
		dev->audio_stream = intf;

	}
	if (intf->cur_altsetting->desc.bInterfaceNumber == AUDIO_CONTROL_INTERFACE)
	{

		dev->audio_ctrl = intf;
		stream_intf = usb_ifnum_to_if(dev->udev, AUDIO_STREAMING_INTERFACE);
		if (!stream_intf) return -ENODEV;
		int ret = usb_driver_claim_interface(&usb_audio_drv, stream_intf, dev);
		if (ret) {
			dev_err(&intf->dev, "claim stream intf failed: %d\n", ret);
			return ret;
		}
		dev_info(&stream_intf->dev, "Claimed Streaming interface");
		ret = uac_parse_feature_unit(dev);
		if (ret)
		{
			dev_err(&intf->dev, "failed to find feature unit id: %d\n", ret);
			return ret;

		}
	}
	// assuming ctrl always gets enumerated before
	if (stream_intf)
	{
		usb_register_dev(stream_intf, &uac_class);
		// turn the streaming on
		if (stream_intf->cur_altsetting->desc.bAlternateSetting == AUDIO_STREAMING_ALT_SETTING_STREAM_OFF)
		{
			usb_set_interface(dev->udev, AUDIO_STREAMING_INTERFACE, AUDIO_STREAMING_ALT_SETTING_STREAM_on);
		}
		ret = uac_find_iso_out_ep(stream_intf, &dev->ep_out);
		if (ret) return ret;
	}

	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->wait_queue);
	ret = kfifo_alloc(&dev->audio_ring, AUDIO_RING_SIZE, GFP_KERNEL);
	dev->shutting_down = false;
	if (ret) return ret;
	usb_set_intfdata(intf, dev);
	// allocate urb buffers
	if (stream_intf)
	{
		for (int i = 0; i < NUM_OF_URBS; i++)
		{
			struct urb * urb = usb_alloc_urb(NUM_OF_PACKETS, GFP_KERNEL);
			if (!urb) return -ENOMEM;
			urb->dev = udev;

			urb->complete = uac_callback;
			urb->context = dev;
			urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
			urb->interval = 1;
			urb->number_of_packets = NUM_OF_PACKETS;
			urb->pipe = usb_sndisocpipe(udev, dev->ep_out); // OUT playback sink
			urb->transfer_buffer = usb_alloc_coherent(udev, BUF_SIZE*NUM_OF_PACKETS, GFP_KERNEL, &urb->transfer_dma);

			if (!urb->transfer_buffer)
			{
				usb_free_urb(urb);
				return -ENOMEM;
			}
			urb->transfer_buffer_length = BUF_SIZE*NUM_OF_PACKETS;
			for (int j = 0; j < NUM_OF_PACKETS; j++)
			{
				urb->iso_frame_desc[j].offset = j * BUF_SIZE;
				urb->iso_frame_desc[j].length = BUF_SIZE;
			}
			dev->urbs[i] = urb;
			if (usb_submit_urb(urb, GFP_KERNEL))
			{
				dev_err(&intf->dev, "uac: failed to submit urb\n");
			}
		}
	}

	dev_info(&intf->dev, "uac: initialized\n");
	return 0;
}
static void uac_disconnect(struct usb_interface *intf)
{
	struct uac_dev *dev = usb_get_intfdata(intf);
	int ifnum;

	if (!dev)
		return;

	ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	/* stop future resubmits before killing URBs */
	dev->shutting_down = true;

	/* clear THIS interface's intfdata; leave the peer to usbcore */
	usb_set_intfdata(intf, NULL);

	if (ifnum == AUDIO_STREAMING_INTERFACE) {
		/* 1) remove /dev/uac%d first so userspace can’t re-open */
		usb_deregister_dev(intf, &uac_class);

		/* 2) kill & free URBs + their coherent buffers */
		for (int i = 0; i < NUM_OF_URBS; i++) {
			struct urb *urb = dev->urbs[i];
			if (!urb) continue;

			usb_kill_urb(urb);  /* waits for completion to return */

			if (urb->transfer_buffer)
				usb_free_coherent(dev->udev,
								  BUF_SIZE * NUM_OF_PACKETS,
								  urb->transfer_buffer,
								  urb->transfer_dma);

			usb_free_urb(urb);
			dev->urbs[i] = NULL;
		}

		/* 3) wake waiters and free FIFOs */
		wake_up_interruptible(&dev->wait_queue);
		kfifo_free(&dev->audio_ring);

		/* If you used manual kzalloc for uac_dev (Option B), free it now */
		// kfree(dev);

	} else if (ifnum == AUDIO_CONTROL_INTERFACE) {
		/* AC path: no heavy teardown. Just clearing its own intfdata above is enough. */
	}
}
static const struct usb_device_id uac_ids[] = {
	{USB_DEVICE(USB_AUDIO_VENDOR_ID, USB_AUDIO_PRODUCT_ID)},
	{0,},
};
MODULE_DEVICE_TABLE(usb, uac_ids); // exports to the user space of the supported devices


static struct usb_driver usb_audio_drv = {
	.name = DRIVER_NAME,
	.id_table = uac_ids,
	.probe = uac_probe,
	.disconnect = uac_disconnect,
};

module_usb_driver(usb_audio_drv);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION(DRIVER_NAME);