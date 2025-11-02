#ifndef _UAC_IOCTL_H
#define _UAC_IOCTL_H

#include <linux/ioctl.h>   /* from UAPI; usable in user space */
#include <linux/types.h>   /* for fixed-width types like __s32 */

/* Pick a unique magic. 'u' is fine if not colliding with others in your system. */
#define UAC_IOC_MAGIC 'u'


/* 1–2: mute controls (use write/read of an integer 0/1) */
#define UAC_IOC_SET_MUTE    _IO(UAC_IOC_MAGIC, 0)   /* mute */
#define UAC_IOC_CLR_MUTE    _IO(UAC_IOC_MAGIC, 1)   /* mute */
#define UAC_IOC_GET_MUTE    _IOR(UAC_IOC_MAGIC, 2, int)   /* get mute: 0/1 */

/* 3–4: volume bounds (reads) */
#define UAC_IOC_GET_MIN_VOL _IOR(UAC_IOC_MAGIC, 3, int)   /* min volume */
#define UAC_IOC_GET_MAX_VOL _IOR(UAC_IOC_MAGIC, 4, int)   /* max volume */

/* 5–6: volume set/get */
#define UAC_IOC_SET_VOL     _IOW(UAC_IOC_MAGIC, 5, int)   /* set volume */
#define UAC_IOC_GET_VOL     _IOR(UAC_IOC_MAGIC, 6, int)   /* get volume */

/* Keep this equal to the highest command number you define */
#define UAC_IOC_MAXNR       6

#endif /* _UAC_IOCTL_H */
