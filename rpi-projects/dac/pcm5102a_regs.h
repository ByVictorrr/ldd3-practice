/* =========================
 * Top-level / global registers
 * =========================
 *
 * These control the I²S block as a whole, not a specific channel.
 * Think: interrupts, global TX/RX enables, core power, global format, DMA.
 */

/* IER – Interrupt Enable Register
 *
 * Enables/disables top-level interrupt generation for the I²S block.
 * - Per-channel IMR(x) still masks individual channel sources.
 * - You usually:
 *   - clear it while configuring,
 *   - then set bits once your ISR is ready.
 */
#define IER      0x000

/* IRER – I²S Receive Enable Register (global RX)
 *
 * Global gate for the RECEIVE path.
 * - IRER=1 allows RX engines to run (subject to per-channel RER(x)).
 * - IRER=0 stops *all* RX activity, regardless of RER(x).
 */
#define IRER     0x004

/* ITER – I²S Transmit Enable Register (global TX)
 *
 * Global gate for the TRANSMIT path.
 * - ITER=1 allows TX engines to run (subject to per-channel TER(x)).
 * - ITER=0 stops *all* TX activity, regardless of TER(x).
 * - In your driver you use this as “enable TX side of the block”.
 */
#define ITER     0x008

/* CER – Component Enable Register
 *
 * Master on/off for the entire I²S core (TX+RX+logic).
 * - CER=1: block is powered/clocked and can move data.
 * - CER=0: block is logically off / idle; safe to reconfigure.
 * - Typical sequence:
 *   - CER=0, program CCR/TCR/RCR/FIFOs/DMACR, then CER=1.
 */
#define CER      0x00C

/* CCR – Clock/Configuration Register (global format)
 *
 * Global configuration for:
 * - Word length (16/24/32-bit).
 * - Frame format (I²S, left-justified, etc).
 * - Number of slots per frame, etc. (depends on IP config).
 *
 * You normally:
 * - Set this once at startup or stream open, based on requested format.
 * - Leave it alone while streaming.
 */
#define CCR      0x010

/* DMACR – DMA Control Register (global DMA enable)
 *
 * Enables/disables DMA request generation for RX/TX blocks.
 * - You OR in bits like DMAEN_TXBLOCK / DMAEN_RXBLOCK.
 * - With TX DMA enabled here and TER/ITER/CER set, the core
 *   will assert DMA requests when the TX FIFO needs data.
 */
#define DMACR    0x0200

/* I2S_RXDMA – RX DMA Data Register
 *
 * Data port used by the DMA engine for **receive** transfers.
 * - When DMA_MEM_TO_DEV / DEV_TO_MEM is set up, this is the “device”
 *   address the DMA controller reads/writes to pull/push samples.
 */
#define I2S_RXDMA   0x01C0

/* I2S_RRXDMA – Raw RX DMA (implementation-specific)
 *
 * Often used for additional RX DMA mode or extended features
 * (depending on the exact DesignWare configuration).
 * In many minimal use-cases you won’t touch this directly.
 */
#define I2S_RRXDMA  0x01C4

/* I2S_TXDMA – TX DMA Data Register
 *
 * Data port used by the DMA engine for **transmit** transfers.
 * - This is what you configured as:
 *     config.dst_addr = res->start + I2S_TXDMA;
 * - DMA writes audio samples here; hardware pushes them into TX FIFO.
 */
#define I2S_TXDMA   0x01C8

/* I2S_RTXDMA – Combined RX/TX DMA Register (optional)
 *
 * On some configurations this supports combined/linked RX+TX DMA
 * or additional DMA controls. If you only do simple TX DMA,
 * you can usually ignore it.
 */
#define I2S_RTXDMA  0x01CC


/* =========================
 * Per-channel registers
 * =========================
 *
 * x = channel index (0..3) for multi-channel / TDM configurations.
 * For a simple stereo setup you usually only use channel 0.
 *
 * Note: many of these registers are “dual-use”: when the I²S core
 * is in RX mode they mean one thing (e.g. LRBR = Left RX Buffer);
 * in TX mode the same offset acts as TX FIFO threshold (LTHR).
 */

/* LRBR / LTHR – Left RX Buffer / TX FIFO Threshold
 *
 * - When receiving (RX): LRBR(x) is the LEFT sample receive buffer
 *   for channel x.
 * - When transmitting (TX): LTHR(x) is the TX FIFO threshold register
 *   controlling when interrupts/DMA requests fire for the left side.
 *
 * You typically:
 * - For TX+DMA, program this as a threshold (or use TFCR instead,
 *   depending on IP revision).
 */
#define LRBR_LTHR(x) (0x40 * (x) + 0x020)

/* RRBR / RTHR – Right RX Buffer / TX FIFO Threshold
 *
 * Same idea as LRBR_LTHR but for the RIGHT side of the stereo pair.
 */
#define RRBR_RTHR(x) (0x40 * (x) + 0x024)

/* RER – Receive Enable Register (per channel)
 *
 * Enables RX for channel x.
 * - RER(x).EN = 1: channel x may receive data (if IRER and CER are set).
 * - RER(x).EN = 0: channel x RX is disabled.
 */
#define RER(x)       (0x40 * (x) + 0x028)

/* TER – Transmit Enable Register (per channel)
 *
 * Enables TX for channel x.
 * - TER(x).EN = 1: channel x may transmit data
 *   (if ITER and CER are set).
 * - TER(x).EN = 0: channel x TX is disabled.
 *
 * In your driver:
 * - TER(0) is the “turn on the stereo TX path” switch.
 */
#define TER(x)       (0x40 * (x) + 0x02C)

/* RCR – Receive Configuration Register (per channel)
 *
 * RX-side configuration for channel x:
 * - Slot selection, word size, mono/stereo behavior, etc.
 * - Exact fields depend on RTL config.
 *
 * Typically mirrors TCR(x) but for the receive direction.
 */
#define RCR(x)       (0x40 * (x) + 0x030)

/* TCR – Transmit Configuration Register (per channel)
 *
 * TX-side configuration for channel x:
 * - Word length (e.g., 16-bit vs 32-bit).
 * - Number of active slots/channels.
 * - I²S vs left-justified, etc (depending on where format is split
 *   between CCR and TCR in your IP version).
 *
 * You currently set this to 0x2 for 16-bit (per the datasheet/driver).
 */
#define TCR(x)       (0x40 * (x) + 0x034)

/* ISR – Interrupt Status Register (per channel)
 *
 * Latched interrupt status bits for channel x:
 * - FIFO empty/threshold reached.
 * - Overrun/underrun.
 * - Other per-channel events.
 *
 * Reading this usually:
 * - Lets you see what caused an interrupt.
 * - Often clears bits (read-to-clear).
 */
#define ISR(x)       (0x40 * (x) + 0x038)

/* IMR – Interrupt Mask Register (per channel)
 *
 * Interrupt mask for channel x:
 * - Bit = 0: interrupt source enabled.
 * - Bit = 1: interrupt source masked/disabled.
 *
 * You use this to enable only the IRQ sources you care about
 * (e.g., TX FIFO threshold, RX FIFO threshold).
 */
#define IMR(x)       (0x40 * (x) + 0x03C)

/* ROR – RX Overrun Register
 *
 * RX FIFO overrun flags for channel x:
 * - Set when incoming data arrived but the FIFO was already full
 *   (software too slow).
 * - Often write-1-to-clear or read-to-clear (check your IP docs).
 */
#define ROR(x)       (0x40 * (x) + 0x040)

/* TOR – TX Overrun / Underrun Register
 *
 * TX underrun/overrun flags for channel x:
 * - Most commonly used as **TX underrun**: core tried to take a
 *   sample from an empty FIFO (software/DMA too slow).
 * - Good debug indicator if you’re not feeding data fast enough.
 */
#define TOR(x)       (0x40 * (x) + 0x044)

/* RFCR – RX FIFO Control Register
 *
 * RX FIFO control for channel x:
 * - Set RX FIFO trigger thresholds.
 * - Reset/flush RX FIFO.
 * - Configure DMA/IRQ thresholds, depending on IP revision.
 */
#define RFCR(x)      (0x40 * (x) + 0x048)

/* TFCR – TX FIFO Control Register
 *
 * TX FIFO control for channel x:
 * - Set TX FIFO trigger thresholds.
 * - Reset/flush TX FIFO.
 * - Configure DMA/IRQ thresholds.
 *
 * In your code:
 *   writel(BURST_SIZE - 1, dev->base + TFCR(STEREO_CHANNEL));
 * means “fire DMA/IRQ when the FIFO level crosses this threshold”.
 */
#define TFCR(x)      (0x40 * (x) + 0x04C)

/* Convenience alias – TX DMA data port (same as I2S_TXDMA above) */
#define I2S_TXDMA    0x01C8
