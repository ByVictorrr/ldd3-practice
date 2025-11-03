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
#include "uac.h"


#define DRIVER_NAME "uac"

#define AUDIO_CONTROL_INTERFACE 0
#define AUDIO_STREAMING_INTERFACE 1
#define AUDIO_STREAMING_ALT_SETTING_STREAM_OFF 0
#define AUDIO_STREAMING_ALT_SETTING_STREAM_on 1

#define BUF_SIZE 192
#define NUM_OF_PACKETS 16
#define AUDIO_RING_SIZE 64*1024
#define USB_AUDIO_VENDOR_ID    0x46f4
#define USB_AUDIO_PRODUCT_ID     0x0002


static struct usb_driver usb_audio_drv;

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


extern long uac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
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

extern int uac_parse_feature_unit(struct uac_dev *dev);
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