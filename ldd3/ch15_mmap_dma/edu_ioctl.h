#ifndef _EDU_H
#define _EDU_H

#include <linux/ioctl.h>

struct edu_stream_desc {
    __u64 user_addr;  /* userspace pointer */
    __u32 length;     /* bytes */
    __u32 dir;        /* EDU_DMA_DIR_* */
};

#define EDU_IOC_MAGIC   'E'
#define EDU_IOC_DMA_TX  _IOW(EDU_IOC_MAGIC, 1, __u32)  /* RAM -> EDU */
#define EDU_IOC_DMA_RX  _IOW(EDU_IOC_MAGIC, 2, __u32)  /* EDU -> RAM */
#define EDU_IOC_STREAM_DMA _IOW(EDU_IOC_MAGIC, 3, struct edu_stream_desc)

// COMMANDS
#define EDU_DMA_CMD_START    (1u << 0)  /* 0x01: write 1 to start; poll bit 0 for completion */
#define EDU_DMA_CMD_DIR      (1u << 1)  /* 0x02: 0 = RAM→EDU (host→device), 1 = EDU→RAM (device→host) */
#define EDU_DMA_CMD_IRQ      (1u << 2)  /* 0x04: raise IRQ when done */

/* Optional helpers for readability */
#define EDU_DMA_DIR_RAM_TO_DEV   (0u)           /* clear DIR bit */
#define EDU_DMA_DIR_DEV_TO_RAM   (EDU_DMA_CMD_DIR)

#define EDU_DMA_CMD_BUILD(dir/*0/1*/, irq) \
(EDU_DMA_CMD_START | ((dir)?EDU_DMA_DIR_DEV_TO_RAM: EDU_DMA_DIR_DEV_TO_RAM) | ((irq)?EDU_DMA_CMD_IRQ:0))



#endif