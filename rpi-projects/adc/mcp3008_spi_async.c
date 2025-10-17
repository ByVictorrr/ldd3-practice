#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
/* module param: period in ms; 0 = no periodic sampling */
#define FIFO_SAMPLES 1024

static unsigned int period_ms = 50;
module_param(period_ms, uint, 0644);


struct mcp3008_data{
	struct spi_device *spi; // spi device handle
	unsigned int curr_channel; // current ch (0-7) for future use

	u8 tx[3], rx[3];
	struct spi_transfer xfer;
	struct spi_message msg;
	// completion for async
	struct completion done;
	/* Periodic sampling via regular timer */
	struct timer_list timer;
	struct work_struct work;
	atomic_t in_progress;
	struct kfifo *fifo;
	spinlock_t fifo_lock;
}mcp3008_dev;

static void mcp3008_prep_cmd(struct mcp3008_data *d){
	u8 channel = d->curr_channel & 0x07;
	d->tx[0] = 0x01; // start bit = 1, single/diff = 1 (in next byte)
	d->tx[1] = ((0x8 | channel) << 4); // 0x8 = 1000b sets single-eneded, channel bits << 4
	d->tx[2] = 0x00; // third byte doesnt matter - dummy
}
static void mcp3008_async_complete(void *ctx){
	/* here we fill in the fifo */
	struct mcp3008_data *d = ctx;
	u16 value = ((d->rx[1] & 0x03) << 8) | d->rx[2];
	kfifo_in_spinlocked(&d->fifo, &value, 1, &d->fifo_lock);
	// wake up readers in read function
	atomic_set(&d->in_progress, 0);
	// lastly signal completion
	complete(&d->done);
}
static void mcp3008_work(struct work_struct *w){

	int ret;
	struct mcp3008_data *d = container_of(w, struct mcp3008_data, work);
	// if in_progress == 0 set it to 1; return previous ptr
	if (atomic_cmpxchg(&d->in_progress, 0, 1) != 0)
		return;
	mcp3008_prep_cmd(d);
	reinit_completion(&d->done);
	ret = spi_async(d->spi, &d->msg);
	if (ret){
		dev_err(&d->spi->dev, "SPI transfer failed: %d\n", ret);
		atomic_set(&d->in_progress, 0);
		complete(&d->done);
	}
}

static void mcp3008_timer(struct timer_list *t){
	/* Basically the trigger for reading the adc value*/
	struct mcp3008_data *d = from_timer(d, t, timer);
	schedule_work(&d->work);
	mod_timer(&d->timer, jiffies + msecs_to_jiffies(period_ms));
}


static ssize_t mcp3008_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
	struct mcp3008_data * data = (struct mcp3008_data *)f->private_data;
	char buf[FIFO_SAMPLES][5];
	int n, len, num_samples;
	u16 samples[FIFO_SAMPLES];

	/* basically show to the user whatever you can fit in their buffer */
	if (kfifo_is_empty(&data->fifo))
	{
		// if non blocking just return come back
		if (f->f_flags & O_NONBLOCK) return -ERESTARTSYS;
		// wait for a completion or interuupt
		if (wait_for_completion_interruptible(&data->done)) return -ERESTARTSYS;

	}
	// give data here
	n = kfifo_out_locked(&data->fifo, &samples, ARRAY_SIZE(samples), &data->fifo_lock);
	// see how many characters they can accepts

	num_samples = min(n, count/5);
	if (!num_samples) return -ERESTARTSYS;

	for (int i=0; i < num_samples; i++)
	{
		len += scnprintf(buf[i], sizeof(buf[i]), "%d\n", samples[i]);
	}


	if (len < count) count = len;
	if(copy_to_user(ubuf, buf, len)) return -EFAULT;
	*ppos += count;
	return count;
}

static int mcp3008_open(struct inode *inode, struct file *file){
	struct mcp3008_data *data = &mcp3008_dev;
	file->private_data = data;
	dev_dbg(&data->spi->dev, "Device opened\n");
	return 0;
}
static int mcp3008_release(struct inode *inode, struct file *file){
	struct mcp3008_data *data = &mcp3008_dev;
	dev_dbg(&data->spi->dev, "Device Closed\n");
	return 0;
}

static const struct file_operations mcp3008_fops = {
	.owner  = THIS_MODULE,
	.read   = mcp3008_read,
	.open   = mcp3008_open,
	.release = mcp3008_release,
	.write  = NULL,
	.llseek = noop_llseek,
};

static struct miscdevice misc_mcp3008_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "mcp3008",
	.fops  = &mcp3008_fops,
	.mode  = 0444, /* read-only */
};


static int mcp3008_probe(struct spi_device *spi) {
	int ret;
	struct mcp3008_data *data = &mcp3008_dev;
	dev_info(&spi->dev, "MCP3008 SPI device probed (bus %s)\n", dev_name(&spi->dev));
	data->spi = spi;
	data->curr_channel = 0;
	// optional: set SPI and speed if not set by DT
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = 1000000;
	spi->bits_per_word = 8;
	spi_setup(spi); // apply the changes
	spin_lock_init(&data->fifo_lock);
	atomic_set(&data->in_progress, 0);

	memset(&data->xfer, 0, sizeof(data->xfer));
	data->xfer.tx_buf = data->tx;
	data->xfer.rx_buf = data->rx;
	data->xfer.len = 3;

	init_completion(&data->done);
	spi_message_init(&data->msg);
	ret = kfifo_alloc(&data->fifo, FIFO_SAMPLES * sizeof(u16), GFP_KERNEL);
	if (ret)
		return ret;
	data->msg.complete = mcp3008_async_complete;
	data->msg.context = data;
	spi_message_add_tail(&data->xfer, &data->msg);
	INIT_WORK(&data->work, mcp3008_work);
	// setup timer
	timer_setup(&data->timer, mcp3008_timer, 0);
	if (period_ms > 0)
		mod_timer(&data->timer, jiffies + msecs_to_jiffies(period_ms));




	misc_register(&misc_mcp3008_dev);
	spi_set_drvdata(spi, data);

    return 0;
}

static void mcp3008_remove(struct spi_device *spi) {
	struct mcp3008_data *d= spi_get_drvdata(spi);
	misc_deregister(&misc_mcp3008_dev);
	flush_work(&d->work);
	del_timer_sync(&d->timer);
	kfifo_free(d->fifo);
    printk(KERN_INFO "Unloading MCP3008!\n");
}
static const struct of_device_id mcp3008_dt_ids[] = {
{ .compatible = "tutorial,mcp3008", },
{}
};

MODULE_DEVICE_TABLE(of, mcp3008_dt_ids);
static const struct spi_device_id mcp3008_ids[] = {
    { "tutorial,mcp3008", 0 },  /* matches your DT-compatible modalias */
    { "mcp3008", 0 },           /* optional fallback */
   };
static struct spi_driver mcp3008_driver = {
	.driver = {
    	.name = "mcp3008-char",
    	.of_match_table = mcp3008_dt_ids,
	},
	.id_table = mcp3008_ids,
    .probe = mcp3008_probe,
    .remove = mcp3008_remove,
};

module_spi_driver(mcp3008_driver);
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("MCP3008 ADC SPI Driver - misc device interface");
MODULE_LICENSE("GPL");
