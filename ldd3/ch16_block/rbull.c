#include <linux/module.h>
#include <linux/init.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("victor delaplaine");
MODULE_DESCRIPTION("rbull");
static int rbull_major = 0;

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
	struct queue_limits qlimits;
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;


	/* block control plane */
	int major;
	spinlock_t lock;
	atomic_t open_count;
} r_dev;
struct rbull_rq_ctx
{
	struct rbull_dev *dev;
	struct request *rq;
	struct completion done;
};

static int rbull_open(struct gendisk *disk, blk_mode_t mode){
	struct rbull_dev *dev = disk->private_data;
	spin_lock(&dev->lock);
	if (!atomic_read(&dev->open_count))
	{

		// revalidate disk(gd)
	}
	atomic_inc(&dev->open_count);
	spin_unlock(&dev->lock);
	return 0;
}

static void rbull_release(struct gendisk *disk)
{
	struct rbull_dev *dev = disk->private_data;
	atomic_dec(&dev->open_count);

}
/*
 * The device operations structure.
 */
static struct block_device_operations rbull_fops = {
	.owner           = THIS_MODULE,
	.open 	         = rbull_open,
	.release 	 = rbull_release,
};

static blk_status_t rbull_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bdq)
{
	struct request *rq = bdq->rq;
	struct rbull_dev *dev = rq->q->queuedata;
	sector_t start = blk_rq_pos(rq); // start sector
	unsigned int nr_sectors = blk_rq_sectors(rq); // total amount of sectors
	blk_status_t ret = BLK_STS_OK;
	size_t dev_off = start << SECTOR_SHIFT;
	struct rbull_rq

	/* tell the blk-mq layer that the request is in flight */
	blk_mq_start_request(rq);
	if (blk_rq_is_passthrough(rq))
	{
		// dont support
		ret = BLK_STS_IOERR;
		blk_mq_end_request(rq, ret);
		return ret;

	}
	struct bio_vec bvec;
	struct req_iterator iter;
	rq_for_each_segment(bvec, rq, iter)
	{
		unsigned long offset = bvec.bv_offset;
		unsigned int bytes = bvec.bv_len;
		void *buf = kmap_local_page(bvec.bv_page);
		if (dev->size_bytes < dev_off + bytes)
		{
			ret = BLK_STS_IOERR;
			blk_mq_end_request(rq, ret);
			break;
		}

		spin_lock(&dev->lock);
		switch (req_op(rq))
		{
		case REQ_OP_READ:
			memcpy(buf + offset, dev->data + dev_off, bytes);
			break;
		case REQ_OP_WRITE:
			memcpy(dev->data + dev_off, buf + offset, bytes);
			break;
		case REQ_OP_DISCARD:
		case REQ_OP_WRITE_ZEROES:
			memset(dev->data + dev_off, 0, bytes);
			break;
		default:
			ret = BLK_STS_IOERR;
		}
		spin_unlock(&dev->lock);
		dev_off += bytes;
		kunmap_local(buf);
	}
	blk_mq_end_request(rq, ret); // this is used to call bi_ioend callback wake up waiting processes
	return ret;

}
struct blk_mq_ops rbull_mq_ops = {
	.queue_rq =  rbull_queue_rq,
	.init_request =
};

static int setup_queue_limit(struct rbull_dev * dev)
{
	struct queue_limits *ql = &dev->qlimits;
	u32 lba_sectors;
	/* validate sector_size param */
	if (sector_size < SECTOR_SIZE || !is_power_of_2(sector_size)) return -EINVAL;
	lba_sectors = sector_size >> SECTOR_SHIFT;
	if (!lba_sectors) lba_sectors = 1;

	blk_set_stacking_limits(ql); // set stacking limits
	memset(ql, 0, sizeof(*ql));
	ql->features = 0;
	// ql->features = (~BLK_FEAT_ZONED | ~BLK_FEAT_ROTATIONAL | ~BLK_FEAT_WRITE_CACHE | ~BLK_FEAT_FUA );

	/* sector and block size */
	ql->logical_block_size = sector_size;
	ql->physical_block_size = sector_size;

	/* request sizing: max sectors*/
	ql->max_hw_sectors = 8; // a request can only span 8 sectors
	ql->max_sectors = 8; // must be less than or equal to hw

	/* dma scatter-gather: noop */
	ql->max_segments = 10;
	ql->max_segment_size = PAGE_SIZE;
	return 0;

}
static int __init rbull_init(void){
	int ret;
	/* Allocate & init backing memory */
	struct rbull_dev *dev = &r_dev;
	memset(dev, 0, sizeof(struct rbull_dev));
	spin_lock_init(&dev->lock);
	atomic_set(&dev->open_count, 0);

	dev->size_bytes = (u64)disk_sectors_mb * 1024 * 1024;
	dev->capacity_sectors = dev->size_bytes >> SECTOR_SHIFT;
	dev->data = vmalloc(dev->size_bytes);
	if (!dev->data) return -ENOMEM;
	memset(dev->data, 0, dev->size_bytes);
	/* Initialize the tag set */
	memset(&dev->tag_set, 0, sizeof(struct blk_mq_tag_set));
	dev->tag_set.ops = &rbull_mq_ops;
	dev->tag_set.nr_hw_queues = nr_hw_queues; // only maps to how many cpu's are there rest are unused
	dev->tag_set.queue_depth = queue_depth; // 256 commands/requests per tag's
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.cmd_size = sizeof(struct rbull_rq_ctx); // request's private data
	dev->tag_set.nr_maps = 1; // default no polling cpu to hwctx maps
	/* allocate or tell blk-mq to create queues and mappings*/
	ret = blk_mq_alloc_tag_set(&dev->tag_set);
	if (ret < 0) goto free_data;
	ret = setup_queue_limit(dev);
	if (ret) goto free_tag_set;

	/* Allocates disk and request_queue */
	dev->disk = blk_mq_alloc_disk(&dev->tag_set, &dev->qlimits, dev);
	if (IS_ERR(dev->disk))
	{
		ret = PTR_ERR(dev->disk);
		printk(KERN_WARNING "rbull: blk_mq_alloc_disk failed %d\n", ret);
		goto free_tag_set;
	}
	dev->disk->major = 0;
	dev->disk->fops = &rbull_fops;
	dev->disk->private_data = dev;
	snprintf(dev->disk->disk_name, sizeof(dev->disk->disk_name), "rbull%d", rbull_major);
	memcpy(dev->disk->disk_name, "rbull0", sizeof("rbull0"));
	set_capacity(dev->disk, dev->capacity_sectors);
	ret = add_disk(dev->disk);
	if (ret < 0)
	{
		printk(KERN_WARNING "rbull: add_disk failed\n");
		goto free_disk;
		return ret;

	}
	return 0;

	free_disk:
	    put_disk(dev->disk);
	free_tag_set: // never attached to the system
		blk_mq_free_tag_set(&dev->tag_set);
	free_data:
		vfree(dev->data);
	return ret;
}

static void __exit rbull_exit(void)
{

	struct rbull_dev *dev = &r_dev;
	del_gendisk(dev->disk);
	put_disk(dev->disk);
	blk_mq_free_tag_set(&dev->tag_set);
	vfree(dev->data);
}
module_init(rbull_init)
module_exit(rbull_exit)
