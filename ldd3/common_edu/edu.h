#ifdef _EDU_H_
#define _EDU_H_

#define DMA_BUF_SIZE	4096
#define EDU_VENDOR     0x1234
#define EDU_DEVICE     0x11e8


struct edu_dev
{
    struct pci_dev *pdev;
    void __iomem *bar0;
    /* locking & sync */
    struct mutex xfer_lock;      /* serialize DMA starts in process context */
    spinlock_t    isr_lock;      /* protect in_flight/irq-shared fields */
    struct completion done;      /* signals DMA completion */
    bool in_flight;
    /* dma address */
    dma_addr_t dma_handle;
    void * dma_buf;
    /* save dma buffer */
    char save_state[DMA_BUF_SIZE];

};


/* ---BAR 0 register maps---- */

// low MMIO

/* RO: returns version ID */
#define EDU_BAR0_ID_REG 0x00
/* RW: write any 32-bit value; reading returns its bitwise inverse */
#define EDU_BAR0_LIVENESS_REG 0x04
/* RW: write N; device writes back N! after you clear the 'factial busy bit' in STATUS */
#define EDU_BAR0_FACTORIAL_REG 0x08

/* RW: Bit-OR field: 0x01 = factorial busy (RO); 0x80= raise IRQ when factorial completes*/
#define EDU_BAR0_STATUS_REG 0x20
/* RO: latches value written in 'raise' 0x60, you clear vbits via 'ack' (0x64) */
#define EDU_BAR0_IRQ_STATUS_REG 0x24
/* WO: write any value to trigger an IRQ; value is OR'ed into 0x24 */
#define EDU_BAR0_IRQ_RAISE_REG  0x60
/* WO: write same value you saw in 0x24 to drop the interrupt. must be done in ISR */
#define EDU_BAR0_IRQ_ACK_REG    0x64

// DMA engine registers


/* RW: Host physical address */
#define EDU_BAR0_DMA_SRC_REG    0x80
/* RW: Host physical address */
#define EDU_BAR0_DMA_DST_REG    0x88
/* RW: transfer size */
#define EDU_BAR0_DMA_COUNT_REG    0x90
/* RW Bit-OR: 0x01 start, 0x20 dir (0=RAM->EDU, 1=EDU-RAM), 0x04 raise IRQ (0x100 when done; poll bit 0 to wait for completion*/
#define EDU_BAR0_DMA_CMD_REG    0x98

// 4KiB
#define EDU_BAR0_DMA_BUFFER_REG 0x40000

// COMMANDS
#define EDU_DMA_CMD_START    (1u << 0)  /* 0x01: write 1 to start; poll bit 0 for completion */
#define EDU_DMA_CMD_DIR      (1u << 1)  /* 0x02: 0 = RAM→EDU (host→device), 1 = EDU→RAM (device→host) */
#define EDU_DMA_CMD_IRQ      (1u << 2)  /* 0x04: raise IRQ when done */

/* Optional helpers for readability */
#define EDU_DMA_DIR_RAM_TO_DEV   (0u)           /* clear DIR bit */
#define EDU_DMA_DIR_DEV_TO_RAM   (EDU_DMA_CMD_DIR)
#define EDU_DMA_CMD_BUILD(dir/*0/1*/, irq) \
(EDU_DMA_CMD_START | ((dir)?EDU_DMA_CMD_DIR:0) | ((irq)?EDU_DMA_CMD_IRQ:0))

#endif
