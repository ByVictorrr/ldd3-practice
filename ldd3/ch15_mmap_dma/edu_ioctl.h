#ifndef _EDU_IOCTL_H
#define _EDU_IOCTL_H

#include <linux/types.h>
struct edu_stream_desc {
    __u64 user_addr;  /* userspace pointer */
    __u32 length;     /* bytes */
    __u32 dir;        /* EDU_DMA_DIR_* */
};

#define EDU_IOC_MAGIC   'E'
#define EDU_IOC_DMA_TX  _IOW(EDU_IOC_MAGIC, 1, __u32)  /* RAM -> EDU */
#define EDU_IOC_DMA_RX  _IOW(EDU_IOC_MAGIC, 2, __u32)  /* EDU -> RAM */
#define EDU_IOC_STREAM_DMA _IOW(EDU_IOC_MAGIC, 3, struct edu_stream_desc)



#endif