#ifndef UAC_DEV_H
#define UAC_DEV_H
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/wait.h>

#define NUM_OF_URBS 8
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
#endif
