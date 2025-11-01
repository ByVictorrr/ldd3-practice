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
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>


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


static struct usb_driver usb_audio_drv;

struct uac_dev
{
	struct usb_device *udev;

	struct usb_interface *audio_ctrl;
	struct usb_interface *audio_stream;

	spinlock_t lock;
	bool in_flight;
	struct usb_anchor anchor;
	struct kfifo audio_ring;
	struct wait_queue_head wait_queue;
	struct dentry *debugfs_root;

};

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
	pr_info("uac: reading %d bytes\n", (int)count);
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
		return -ENODEV;                   /* minor not bound */ } filp->private_data = dev;
	return 0;
}

static int uac_release(struct inode *inode, struct file *filp)
{

	return 0;
}

static const struct file_operations uac_fops = {
	.owner  = THIS_MODULE,
	.open   = uac_open,
	.read   = uac_read,
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
	struct uac_dev * dev = urb->context;
	if (urb->status)
	{
		dev_err(&dev->udev->dev,"uac: urb status %d\n", urb->status);
		return;
	}
	for (int i = 0; i < urb->number_of_packets; i++)
	{

		spin_lock(&dev->lock);
		// process the packet
		// dev_info(&dev->udev->dev, "uac: got packet %d size of %d\n", i, urb->iso_frame_desc[i].actual_length);
		kfifo_in(&dev->audio_ring,
		 (u8 *)urb->transfer_buffer + urb->iso_frame_desc[i].offset,
		 urb->iso_frame_desc[i].actual_length);

		spin_unlock(&dev->lock);
	}
	wake_up_interruptible(&dev->wait_queue);
	usb_anchor_urb(urb, &dev->anchor);
	usb_submit_urb(urb, GFP_ATOMIC);

}

static int pipe_show(struct seq_file *m, void *v)
{
		//seq_printf(m, "\nDevice %i: %p\n", i, p);
		// seq_printf(m, "   Buffer: %p to %p (%i bytes)\n", p->start, p->end, p->buffersize);
		// seq_printf(m, "   rp %p   wp %p\n", p->rp, p->wp);
		// seq_printf(m, "   readers %i   writers %i\n", p->nreaders, p->nwriters);
		// up(&p->sem);
	return 0;
}
static int my_open(struct inode *inode, struct file *file)
{
	return single_open(file, pipe_show, NULL);
}


static const struct file_operations uac_debug_ops = {
	.owner   = THIS_MODULE,
	.open    = my_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};
static int uac_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int ret;

	struct uac_dev * dev = devm_kzalloc(&intf->dev, sizeof(*dev), GFP_KERNEL);
	struct usb_interface *stream_intf = NULL;
	struct usb_device *udev = interface_to_usbdev(intf);
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
	}

	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->wait_queue);
	dev->in_flight = false;
	ret = kfifo_alloc(&dev->audio_ring, AUDIO_RING_SIZE, GFP_KERNEL);
	if (ret) return ret;
	init_usb_anchor(&dev->anchor);
	usb_set_intfdata(intf, dev);
	dev->debugfs_root = debugfs_create_dir("uac_ctrls", NULL);
	// allocate urb buffers
	if (stream_intf)
	{
		// const struct usb_endpoint_descriptor *epd = &stream_intf->cur_altsetting->endpoint[0].desc;
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
			urb->pipe = usb_sndisocpipe(udev, 0x1); // OUT playback sink
			// urb->pipe = usb_sndisocpipe(udev, epd->bEndpointAddress & 0x0F);
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
			usb_anchor_urb(urb, &dev->anchor);
			if (usb_submit_urb(urb, GFP_KERNEL))
			{
				dev_err(&intf->dev, "uac: failed to submit urb\n");
			}
		}
	}
	char tmp[64];
	memset(tmp, 0, sizeof(tmp));
	scnprintf(tmp, sizeof(tmp), "mute");
	debugfs_create_file(tmp, 0644, dev->debugfs_root, NULL, &uac_debug_ops);
	dev_info(&intf->dev, "uac: initialized\n");
	return 0;
}

static void uac_disconnect(struct usb_interface *intf)
{
	// struct uac_dev * dev = usb_get_intfdata(intf);
	// usb_driver_release_interface(&usb_audio_drv, dev->audio_stream);



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