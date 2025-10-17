#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/errno.h>

struct mcp3008_data{
	struct spi_device *spi; // spi device handle
	unsigned int curr_channel; // current ch (0-7) for future use
}mcp3008_dev;

static ssize_t mcp3008_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
	struct mcp3008_data * data = f->private_data;
	u8 tx[3], rx[3];
	// Can have multiple but one transfer, then next happens in message
	struct spi_transfer xfer = { // represents one transfer (while CE is held high)
		.tx_buf = tx,
		.rx_buf = rx,
		.len = 3,
		.cs_change = 0,
		.bits_per_word = 8,
	};
	struct spi_message msg;
	int ret;
	u16 value;
	char result_str[5]; // enough to hold "1023\n"
	size_t len;
	// only allow reading once per open (simulate eof after one read)
	if (*ppos != 0)
		return 0;
	// prepare SPI command to read from the current channel (single-eneded)
	u8 channel = data->curr_channel & 0x07;
	tx[0] = 0x01; // start bit = 1, single/diff = 1 (in next byte)
	tx[1] = ((0x8 | channel) << 4); // 0x8 = 1000b sets single-eneded, channel bits << 4
	tx[2] = 0x00; // third byte doesnt matter - dummy
	spi_message_init(&msg);
	// add transfer to message
	spi_message_add_tail(&xfer, &msg);
	// start the transfer, block until done
	ret = spi_sync(data->spi, &msg);
	if (ret < 0){
		dev_err(&data->spi->dev, "SPI transfer failed: %d\n", ret);
		return ret;
	}
	// THe CP3008 returns 10-bit result in rx[1] & rx[2]
	// rx[1] [x|x|x|x|x|x|d9|d8]
	// rx[2] [d7|d6|d5|d4|d3|d2|d1|d0]
	value = ((rx[1] & 0x03) << 8) | rx[2];
	dev_info(&data->spi->dev, "Raw SPI bytes: %02x %02x %02x, value=%d\n", rx[0], rx[1], rx[2], value);
	// conver the value to a string
	len = scnprintf(result_str, sizeof(result_str), "%d\n", value);
	// copy to userspace
	if (len > count) len = count; // dont overflow
	if(copy_to_user(ubuf, result_str, len)) return -EFAULT;
	*ppos += len;
	return len;
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

	misc_register(&misc_mcp3008_dev);
	spi_set_drvdata(spi, data);

    return 0;
}

static void mcp3008_remove(struct spi_device *spi) {
	misc_deregister(&misc_mcp3008_dev);
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
