#include <linux/module.h>
#include <linux/init.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>

static int sbull_major = 0;

module_param(sbull_major, int, 0444);

struct sbull_dev{
	struct gendisk *disk;
	spinlock_t lock;
	int users;
	struct blk_mq_tag_set tag_set;

};
struct rbull_rq_ctx
{
	struct sbull_dev *dev;
	struct request *rq;
	struct completion done;
};
static int rbull_open(struct gendisk *disk, blk_mode_t mode){
	struct sbull_dev *dev = disk->private_data;
	spin_lock(&dev->lock);
	if (!dev->users){
	// wait does it do?
		/*
	if (bdev_check_media_change(bd)){
		// revalidate disk(gd)
	*/
	}
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations sbull_ops = {
	.owner           = THIS_MODULE,
	.open 	         = rbull_open,
	.release 	 = NULL,
	.ioctl	         = NULL,
};

struct blk_mq_ops rbull_mq_ops = {

};

static int __init sbull_init(){
	struct sbull_dev *dev = kzalloc(sizeof(struct sbull_dev), GFP_KERNEL);
	// grab major
	sbull_major = register_blkdev(0, "sbull");
	memset(&dev->tag_set, 0, sizeof(struct blk_mq_tag_set));
	dev->tag_set.ops = &rbull_mq_ops;
	dev->tag_set.nr_hw_queues = 2; // only maps to how many cpu's are there rest are unused
	dev->tag_set.queue_depth = 256; // 256 commands/requests per tag's
	dev->tag_set.cmd_size = sizeof(struct rbull_rq_ctx); //?
	dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;
	dev->tag_set.nr_maps = 1; // default no polling cpu to hwctx maps

	struct queue_limits queue_limits;
	// 1 minor
	dev->disk = blk_mq_alloc_disk(&dev->tag_set, &queue_limits, dev);
	dev->disk->major = sbull_major;
	dev->disk->first_minor = 0;

	//dev->disk->fops = &sbull_fops;
	add_disk(dev->disk);



	return 0;
}