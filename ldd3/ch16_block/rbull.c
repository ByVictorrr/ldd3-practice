#include <linux/module.h>
#include <linux/init.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

static int rbull_major = 0;
module_param(rbull_major, int, 0444);
MODULE_PARM_DESC(rbull_major, "Major number (0 = dynamic allocation)");

static u32 sector_size = 512;
module_param(sector_size, uint, 0644);
MODULE_PARM_DESC(sector_size, "Logical sector size in bytes");

static u32 disk_sectors_mb = 64;
module_param(disk_sectors_mb, uint, 0644);
MODULE_PARM_DESC(disk_sectors_mb, "Disk size in MiB (or your module's chosen meaning)");

static u16 nr_hw_queues = 1;
module_param(nr_hw_queues, ushort, 0644);
MODULE_PARM_DESC(nr_hw_queues, "Number of hardware queues");

static u32 queue_depth = 64;
module_param(queue_depth, uint, 0644);
MODULE_PARM_DESC(queue_depth, "Queue depth per hardware queue");

struct rbull_dev{
	/* Backing store stuff */
	void *data; // backing store
	u64 size_bytes;  // backing stores size
	sector_t capacity_sectors; // backing store num of sectors
	/* blk-mq objects */
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;

	/* block control plane */
	int major;
	spinlock_t lock;
	atomic_t open_count;
};
struct rbull_rq_ctx
{
	struct rbull_dev *dev;
	struct request *rq;
	struct completion done;
};

static int rbull_open(struct gendisk *disk, blk_mode_t mode){
	struct rbull_dev *dev = disk->private_data;
	spin_lock(&dev->lock);
	if (!atomic_read(&dev->open_count)){
	// wait does it do?
		/*
	if (bdev_check_media_change(bd)){
		// revalidate disk(gd)
	*/
	}
	atomic_inc(&dev->open_count);
	spin_unlock(&dev->lock);
	return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations rbull_ops = {
	.owner           = THIS_MODULE,
	.open 	         = rbull_open,
	.release 	 = NULL,
	.ioctl	         = NULL,
};

static blk_status_t rbull_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bdq)
{
	struct request *rq = bdq->rq;
	struct rbull_dev *dev = rq->q->queuedata;
	sector_t start = blk_rq_pos(rq); // start sector
	unsigned int nr_sectors = blk_rq_sectors(rq); // total amount of sectors
	blk_status_t ret = BLK_STS_OK;


	/* tell the blk-mq layer that the request is in flight */
	blk_mq_start_request(rq);
	if (blk_rq_is_passthrough(rq))
	{
		// dont support

	}
	struct bio_vec bvec;
	struct req_iterator iter;
	rq_for_each_segment(bvec, rq, iter)
	{
		unsigned long offset = bvec.bv_offset;
		unsigned int bytes = bvec.bv_len;
		void *buf = bvec.bv_page;

		switch (req_op(rq))
		{
		case REQ_OP_READ:
			spin_lock(&dev->lock);
			memcpy(buf + offset, dev->data + (start * sector_size), bytes);
			spin_unlock(&dev->lock);
			break;
		case REQ_OP_WRITE:
			spin_lock(&dev->lock);
			memcpy(dev->data + (start * sector_size), buf + offset, bytes);
			spin_unlock(&dev->lock);
			break;
		case REQ_OP_DISCARD:
		case REQ_OP_WRITE_ZEROES:
			spin_lock(&dev->lock);
			memset(dev->data + (start *sector_size), 0, bytes);
			spin_unlock(&dev->lock);
			break;
		default:
			ret = BLK_STS_IOERR;
		}
		start += bytes / sector_size;
	}
	blk_mq_end_request(rq, ret); // this is used to call bi_ioend callback wake up waiting processes
	return ret;

}
struct blk_mq_ops rbull_mq_ops = {
	.queue_rq =  rbull_queue_rq,
};

static int __init rbull_init(){
	struct rbull_dev *dev = kzalloc(sizeof(struct rbull_dev), GFP_KERNEL);
	// grab major
	rbull_major = register_blkdev(0, "rbull");
	memset(&dev->tag_set, 0, sizeof(struct blk_mq_tag_set));
	dev->tag_set.ops = &rbull_mq_ops;
	dev->tag_set.nr_hw_queues = nr_hw_queues; // only maps to how many cpu's are there rest are unused
	dev->tag_set.queue_depth = queue_depth; // 256 commands/requests per tag's
	dev->tag_set.cmd_size = sizeof(struct rbull_rq_ctx); //?
	dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;
	dev->tag_set.nr_maps = 1; // default no polling cpu to hwctx maps

	struct queue_limits queue_limits;
	// 1 minor
	dev->disk = blk_mq_alloc_disk(&dev->tag_set, &queue_limits, dev);
	dev->disk->major = rbull_major;
	dev->disk->first_minor = 0;

	//dev->disk->fops = &rbull_fops;
	add_disk(dev->disk);

	return 0;
}