#ifndef _UAC_IOCTL_H
#define _UAC_IOCTL_H

/* uac_ioctl.h */
#define UAC_IOC_MAGIC 'u'
#define UAC_IOC_SET_MUTE _IOW(UAC_IOC_MAGIC, 0, int)   /* 0/1 */
#define UAC_IOC_GET_MUTE _IOR(UAC_IOC_MAGIC, 1, int)
#define UAC_IOC_GET_MIN_VOL _IOR(UAC_IOC_MAGIC, 2, int)
#define UAC_IOC_GET_MAX_VOL _IOR(UAC_IOC_MAGIC, 3, int)
#define UAC_IOC_SET_VOL _IOW(UAC_IOC_MAGIC, 4, int)
#define UAC_IOC_GET_VOL _IOR(UAC_IOC_MAGIC, 5, int)
#define UAC_IOC_MAXNR 5

#endif /* _UAC_IOCTL_H */
