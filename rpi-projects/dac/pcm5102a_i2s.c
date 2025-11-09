#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include "pcm5102a_regs.h"

#define DMAEN_RXBLOCK BIT(16)
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
    int16_t *tx_buf;
    size_t tx_buf_len;
    struct platform_device *pdev;
    unsigned period_idx; // which period wer are on
    struct dma_device *dma_dev;
    struct dma_async_tx_descriptor *tx_desc;
};

static void fill_audio_buffer(int16_t *buf, size_t len)
{
    int frames = len / (sizeof(int16_t)*2);
    uint16_t amplitude = 0x7FFF;
    for (int i = 0; i < frames; i++)
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
    // tasklet - softirq context
    struct pcm5102a_dev *dev = data;
    unsigned max_period = dev->tx_buf_len / PERIOD_LEN;
    size_t offset_bytes = dev->period_idx * PERIOD_LEN;
    int16_t *buf = dev->tx_buf + (offset_bytes / sizeof(int16_t));
    fill_audio_buffer(buf, PERIOD_LEN);
    dev->period_idx = (dev->period_idx + 1 ) % max_period;
    dev_info(&dev->pdev->dev, "DMA period %u\n", dev->period_idx);

}
static void pcm5102a_hw_init(struct pcm5102a_dev *dev)
{
    /* Configure the static registers */
    /* 1. Disable Controller - master control */
    writel(0,  dev->base + CER); // down(sem_m)
    /* 2. Tx logic disabled globally */
    writel(0, dev->base + ITER); // down(sem_tx)
    /* 3. Tx logic disabled for channel 0 */
    writel(0, dev->base + TER(STEREO_CHANNEL)); // down(sem_tx0)
    /* 4. Set Transmit configuration for channel 0 */
    writel(0x2, dev->base + TCR(0));  // 0x2 -> word length 16-bit for channel 0
    /* 5.  Globally configure the i2s protocol */
    writel(0, dev->base + CCR); // std i2s protocol, max 16-bit sample data */
    /* 6. Set the TX FIFO threshold */
    // writel(BURST_SIZE - 1, dev->base + TFCR(STEREO_CHANNEL)); // get more data from dma controller when  FIFO_LEN <= BURST_SIZE
    writel(100, dev->base + TFCR(STEREO_CHANNEL)); // get more data from dma controller when  FIFO_LEN <= BURST_SIZE
    /* 7. Enable events to go out to IRQ or DMA ctrl */
    writel(0x1, dev->base + IER); // this would allow us to use the request line to dma when threshold meets
    /* 8. Tell the block to use DMA for TX FIFO filling */
    writel(DMAEN_TXBLOCK, dev->base + DMACR); // enable DMA for tx channel
    /* 9. Clear underrun error flags */
    readl(dev->base + TOR(STEREO_CHANNEL)); // clear underrun flags; maybe from previous run they were bad
    /* 10. Enable the TX logic for channel 0 */
    writel(0x1, dev->base + TER(STEREO_CHANNEL)); //  up(sem_tx0)
    /* 11. Enable the TX logic globally */
    writel(0x1, dev->base + ITER); // up(sem_tx)
    /* 12. Enable the controller */
    writel(0x1, dev->base + CER); // up(sem_m)

}
static int pcm5102a_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct resource *res;
    struct dma_slave_config config;
    dma_cookie_t cookie;
    struct pcm5102a_dev *dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL); if (!dev) return -ENOMEM;
    platform_set_drvdata(pdev, dev);
    dev->pdev = pdev;
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    dev->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(dev->base)) return PTR_ERR(dev->base);
    dev->clk = devm_clk_get(&pdev->dev, NULL);
    if (IS_ERR(dev->clk)) return PTR_ERR(dev->clk);
    clk_prepare_enable(dev->clk);
    ret = clk_set_rate(dev->clk, 48000 * 16 * 2);  // 48k * 16-bit * 2ch = 1.536 MHz
    if (ret)
    {
        dev_err(&dev->pdev->dev, "Failed to set I2S bitclock: %d\n", ret);
        return ret;
    }

    // need the clock to be able to config
    dev->tx_chan = dma_request_chan(&pdev->dev, "tx"); // tx is just the name of the mapping to our request line in dt
    if (IS_ERR(dev->tx_chan)) return -ENODEV;

    // Setup dma buffer
    dev->dma_dev = dev->tx_chan->device;
    dev->tx_buf_len = 2048;
    dev_info(&pdev->dev, "alloc dma buffer len=%zu\n", dev->tx_buf_len);
    // need to use dma controller because iommu needs set page table for it
    dev->tx_buf = dma_alloc_coherent(dev->dma_dev->dev, dev->tx_buf_len, &dev->tx_dma_addr, GFP_KERNEL);
    if (!dev->tx_buf)
    {
        ret=-ENOMEM;
        dev_err(&pdev->dev, "failed to allocate dma buffer\n");
        goto error_release_chan;
    }
    // Generate the Audio waveform
    fill_audio_buffer(dev->tx_buf, dev->tx_buf_len);
    // configure DMA Transfer properties
    // Destination is needed so it knows where to transfer to
    memset(&config, 0, sizeof(config));
    config.direction = DMA_MEM_TO_DEV; // from memory to device
    config.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES; // 16-bit transfers
    config.dst_maxburst = BURST_SIZE; // transfer up to 4 samples per burst
    config.dst_addr = res->start + I2S_TXDMA; // I2S0_TX_FIFO we dont have a iommu to get there no iova is needed
    ret = dmaengine_slave_config(dev->tx_chan, &config);
    if (ret) goto error_free_buf;
    // prepare a cyclic dma descriptor
    dev->tx_desc = dmaengine_prep_dma_cyclic(
        dev->tx_chan,
        dev->tx_dma_addr,
        dev->tx_buf_len,
        PERIOD_LEN,
        DMA_MEM_TO_DEV,
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
        );
   if (!dev->tx_desc)
   {
       ret = -ENOMEM;
       dev_err(&pdev->dev, "failed to prepare dma descriptor\n");
       goto error_free_buf;
   }
    dev->tx_desc->callback = pcm5102a_tx_callback;
    dev->tx_desc->callback_param = dev;
    dev->period_idx = 0;
    cookie = dmaengine_submit(dev->tx_desc);
   if (cookie < 0)
   {
       ret = -EIO;
       dev_err(&pdev->dev, "failed to submit dma descriptor\n");
       goto error_free_buf;
   }
    pcm5102a_hw_init(dev);
    dma_async_issue_pending(dev->tx_chan); // tell dma ctrl to start


    return 0;
    error_free_buf:
        dma_free_coherent(dev->dma_dev->dev, dev->tx_buf_len, dev->tx_buf, dev->tx_dma_addr);
    error_release_chan:
        dma_release_channel(dev->tx_chan);
        clk_disable_unprepare(dev->clk);
    return ret;
}
void pcm5102a_remove(struct platform_device *pdev)
{
    struct pcm5102a_dev *dev = platform_get_drvdata(pdev);
    u32 had_underrun;
    dmaengine_terminate_sync(dev->tx_chan);
    dma_release_channel(dev->tx_chan);

    had_underrun = readl(dev->base + TOR(STEREO_CHANNEL));      // clear TX underrun flags
    if (had_underrun != 0)
    {
        dev_err(&pdev->dev, "had TX underrun flags: %x\n", had_underrun);
    }
    writel(0, dev->base + IER);                    // disable events from going out
    writel(0, dev->base + TER(STEREO_CHANNEL));   // disable TX channel 0
    writel(0, dev->base + ITER);                  // disable global TX
    writel(0, dev->base + DMACR);                 // disable DMA handshake
    /* Disable the component */
    writel(0, dev->base + CER);

    dma_free_coherent(dev->dma_dev->dev, dev->tx_buf_len, dev->tx_buf, dev->tx_dma_addr);

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
        .owner = THIS_MODULE
    },

};
module_platform_driver(p_driver);
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("");
MODULE_LICENSE("GPL");