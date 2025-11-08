#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
/* Common registers (not per-channel) */
#define IER      0x000   // I²S block interrupt enable
#define IRER     0x004   // global RX enable
#define ITER     0x008   // global TX enable
#define CER      0x00C   // component enable
#define CCR      0x010   // global config (word width, etc)
#define DMACR       0x0200
#define I2S_RXDMA           0x01C0
#define I2S_RRXDMA          0x01C4
#define I2S_TXDMA           0x01C8
#define I2S_RTXDMA          0x01CC

/* Per-channel registers (x = channel index 0..3) */
#define LRBR_LTHR(x) (0x40 * (x) + 0x020)  // left RX buf / TX threshold
#define RRBR_RTHR(x) (0x40 * (x) + 0x024)  // right RX buf / TX threshold
#define RER(x)       (0x40 * (x) + 0x028)  // RX Enable for channel x
#define TER(x)       (0x40 * (x) + 0x02C)  // **TX Enable** for channel x
#define RCR(x)       (0x40 * (x) + 0x030)  // RX Config for channel x
#define TCR(x)       (0x40 * (x) + 0x034)  // **TX Config** for channel x
#define ISR(x)       (0x40 * (x) + 0x038)  // Interrupt status (per chan)
#define IMR(x)       (0x40 * (x) + 0x03C)  // Interrupt mask (per chan)
#define ROR(x)       (0x40 * (x) + 0x040)  // RX overrun
#define TOR(x)       (0x40 * (x) + 0x044)  // TX overrun
#define RFCR(x)      (0x40 * (x) + 0x048)  // RX FIFO control
#define TFCR(x)      (0x40 * (x) + 0x04C)  // TX FIFO control

#define DMAEN_RXBLOCK BIT(16) //
#define DMAEN_TXBLOCK BIT(17)

#define STEREO_CHANNEL 0
#define SAMPLES_PER_CYCLE 48
#define BURST_SIZE 4
#define PERIOD_LEN 256

struct pcm5102a_dev
{
    void __iomem *base;
    struct clk *clk;
    struct dma_chan *tx_chan;
    dma_addr_t tx_dma_addr;
    void *tx_buf;
    size_t tx_buf_len;
    struct platform_device *pdev;
    unsigned period_idx; // which period wer are on
};

static void fill_audio_buffer(int16_t *buf, size_t len)
{
    int samples = len / sizeof(int16_t)/2;
    uint16_t amplitude = 0x7FFF;
    for (int i = 0; i < samples; i++)
    {
        // if n goes over the samples_per_cycle need to correct
        int phase = i % SAMPLES_PER_CYCLE;
        static const int16_t sin_table[SAMPLES_PER_CYCLE] = {
            0, 4276, 8480, 12539, 16383, 19947, 23169, 25995,
            28378, 30272, 31650, 32486, 32767, 32486, 31650, 30272,
            28378, 25995, 23169, 19947, 16383, 12539, 8480, 4276,
            0, -4277, -8481, -12540, -16384, -19948, -23170, -25996,
            -28379, -30273, -31651, -32487, -32768, -32487, -31651, -30273,
            -28379, -25996, -23170, -19948, -16384, -12540, -8481, -4277 };
        int16_t sample = sin_table[phase];
        buf[i * 2] = sample;
        buf[i * 2 + 1] = sample;

    }
}
static void pcm5102a_tx_callback(void *data)
{
    struct pcm5102a_dev *dev = data;
    unsigned max_period = dev->tx_buf_len / PERIOD_LEN;
    size_t offset = dev->period_idx * PERIOD_LEN;
    fill_audio_buffer(dev->tx_buf + offset, PERIOD_LEN);
    dev->period_idx = (dev->period_idx + 1 ) % max_period;

}
static int pcm5102a_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct resource *res;
    struct pcm5102a_dev *dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL); if (!dev) return -ENOMEM;
    platform_set_drvdata(pdev, dev);
    dev->pdev = pdev;
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    dev->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(dev->base)) return PTR_ERR(dev->base);
    dev->clk = devm_clk_get(&pdev->dev, NULL);
    if (IS_ERR(dev->clk)) return PTR_ERR(dev->clk);
    clk_prepare_enable(dev->clk);
    // disable TX globably
    writel(0, dev->base + ITER);
    // clear interrupt status
    readl(dev->base + ISR(STEREO_CHANNEL));
    // set the audio resolution - 16 bit samples
    writel(0x2, dev->base + TCR(STEREO_CHANNEL));
    // by default - channels 0 slots 1 and 2 are active for stereo
    // Enable dma for the tx channel globably | channel 0
    // + channel 0, if BIT(8) is TXCH0
    writel(DMAEN_TXBLOCK | BIT(8), dev->base + DMACR);
    // Configure the DMA controller to service us because we can be a bus masterd
    dev->tx_chan = dma_request_chan(&pdev->dev, "tx"); // tx is just the name of the mapping to our request line in dt
    if (IS_ERR(dev->tx_chan)) return -ENODEV;
    // Setup dma buffer
    dev->tx_buf_len = 4096;
    dev->tx_buf = dma_alloc_coherent(&pdev->dev, dev->tx_buf_len, &dev->tx_dma_addr, GFP_KERNEL);
    if (!dev->tx_buf)
    {
        ret=-ENOMEM;
        goto error_release_chan;
    }
    // Generate the Audio waveform
    fill_audio_buffer(dev->tx_buf, dev->tx_buf_len);
    // configure DMA Transfer properties
    // Destination is needed so it knows where to transfer to
    struct dma_slave_config config = {0};
    config.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES; // 16-bit transfers
    config.dst_maxburst = BURST_SIZE; // transfer up to 4 samples per burst
    config.dst_addr = res->start + 0x1C8; // I2S0_TX_FIFO we dont have a iommu to get there no iova is needed
    ret = dmaengine_slave_config(dev->tx_chan, &config);
    if (ret) goto error_free_buf;
    // prepare a cyclic dma descriptor

    struct dma_async_tx_descriptor *tx_desc;
    tx_desc = dmaengine_prep_dma_cyclic(
        dev->tx_chan,
        dev->tx_dma_addr,
        dev->tx_buf_len,
        PERIOD_LEN,
        DMA_DEV_TO_MEM,
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
        );
   if (!tx_desc)
   {
       ret = -ENOMEM;
       goto error_free_buf;
   }
    tx_desc->callback = pcm5102a_tx_callback;
    tx_desc->callback_param = dev;
    dev->period_idx = 0;
   if (dmaengine_submit(tx_desc))
   {
       ret = -EIO;
       goto error_free_buf;
   }
    dma_async_issue_pending(dev->tx_chan); // tell dma ctrl to start

    // enable I2ss transmitter tnow dma is ready
    // enable tx globablly
    writel(1, dev->base + ITER);
    // enable ch 0 tx
    writel(1, dev->base + TER(STEREO_CHANNEL));
    return 0;
    error_free_buf:
        dma_free_coherent(&pdev->dev, dev->tx_buf_len, dev->tx_buf, dev->tx_dma_addr);
    error_release_chan:
        dma_release_channel(dev->tx_chan);
    return ret;
}
void pcm5102a_remove(struct platform_device *pdev)
{
    struct pcm5102a_dev *dev = platform_get_drvdata(pdev);
    // wait to terminate all dma on that channel
    dmaengine_terminate_sync(dev->tx_chan);
    dma_free_coherent(&pdev->dev, dev->tx_buf_len, dev->tx_buf, dev->tx_dma_addr);
    dma_release_channel(dev->tx_chan);

    // stop i2s output
    // disable tx output globally
    writel(0, dev->base + ITER);
    writel(0, dev->base + TER(STEREO_CHANNEL));
    clk_disable_unprepare(dev->clk); // gated
}
static const struct of_device_id pcm5102a_dt_ids[] = {
    {.compatible = "tutorial,pcm5102a"},
    {}
};
MODULE_DEVICE_TABLE(of, pcm5102a_dt_ids);

static struct platform_driver p_driver = {
    .probe = pcm5102a_probe,
    .remove = pcm5102a_remove,
    .driver = {
        .name = "pcm5102a_i2s",
        .of_match_table = pcm5102a_dt_ids,
    },

};
module_platform_driver(p_driver);
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("");
MODULE_LICENSE("GPL");
