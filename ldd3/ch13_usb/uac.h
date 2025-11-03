#ifndef _UAC_DEV_H
#define _UAC_DEV_H
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/wait.h>

#define NUM_OF_URBS 8
struct uac_dev
{
    struct usb_device *udev;

    struct usb_interface *audio_ctrl;
    struct usb_interface *audio_stream;
    struct urb *urbs[NUM_OF_URBS];
    struct kfifo audio_ring;
    struct wait_queue_head wait_queue;
    bool shutting_down;
    spinlock_t lock;
    // ISO OUT endpoint <bit 7 = direction>; bit 0-3= endpoint # (0-15)>
    u8 ep_out;
    /* Control stuff */
    u8 ac_ifnum;
    u8 fu_id;
    s16 max_vol, min_vol, res_vol;


};
#endif
