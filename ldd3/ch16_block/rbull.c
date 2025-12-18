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

static u16 nr_hw_queues = 4;
module_param(nr_hw_queues, ushort, 0644);
MODULE_PARM_DESC(nr_hw_queues, "Number of hardware queues");

static u32 queue_depth = 64;
module_param(queue_depth, uint, 0644);
MODULE_PARM_DESC(queue_depth, "Queue depth per hardware queue");

struct rbull_hctx;
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
	struct rw_semaphore lock;
	atomic_t open_count;
	/* to make flushes possible */
	atomic_t inflight_writes;
	wait_queue_head_t flush_wq;
	/* have a workqueue per hctx */
	struct rbull_hctx **hctxs;
} r_dev;

struct rbull_hctx
{
	struct rbull_dev *dev;
	unsigned int idx;
	/* per-hctx workqueue */
	struct workqueue_struct *wq;
};
static int rbull_open(struct gendisk *disk, blk_mode_t mode){
	struct rbull_dev *dev = disk->private_data;
	atomic_inc(&dev->open_count);
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

struct rbull_rq_ctx
{
	struct rbull_dev *dev;
	struct request *rq;
	size_t dev_off; //bytes offset in backing store
	enum req_op op; // READ/WRITE/FLUSH
	blk_status_t st; // status to complete with
	struct rbull_hctx *rh;
	struct work_struct work;
	/* atomic abort */
	atomic_t abort;
	atomic_t done;

};



static void rbull_rq_work(struct work_struct *work)
{
	struct rbull_rq_ctx *ctx = container_of(work, struct rbull_rq_ctx, work);
	struct request *rq = ctx->rq;
	struct rbull_dev *dev = ctx->dev;
	struct rbull_hctx *rh = ctx->rh;
	sector_t start = blk_rq_pos(rq); // start sector
	blk_status_t ret = BLK_STS_OK;

	struct bio_vec bvec;
	struct req_iterator iter;
	size_t dev_off = start << SECTOR_SHIFT;

	bool is_flush = (ctx->op == REQ_OP_FLUSH);
	bool is_write = op_is_write(ctx->op)  || ctx->op == REQ_OP_DISCARD || ctx->op == REQ_OP_WRITE_ZEROES;
	if (is_flush)
	{
		wait_event(dev->flush_wq, atomic_read(&dev->inflight_writes) == 0);
		goto out_end;

	}




	rq_for_each_segment(bvec, rq, iter)
	{
		unsigned long offset = bvec.bv_offset;
		unsigned int bytes = bvec.bv_len;
		void *buf = kmap_local_page(bvec.bv_page);

		if (dev->size_bytes < dev_off + bytes)
		{
			ret = BLK_STS_IOERR;
			kunmap_local(buf);
			goto out_end;
		}
		if (is_write)
		{
			atomic_inc(&dev->inflight_writes);
			down_write(&dev->lock);
		}else
		{
			down_read(&dev->lock);
		}

		/* Abort early if timeout happened */
		if (atomic_read(&ctx->abort))
		{
			kunmap_local(buf);
			goto abort;
		}


		// here i want to check the direction so i can do direction and then call down write or down read if req_ctx->req
		switch (ctx->op)
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
		case REQ_OP_FLUSH:
			wait_event(dev->flush_wq, atomic_read(&dev->inflight_writes) == 0);
			ret = BLK_STS_OK;
			break;
		default:
			ret = BLK_STS_IOERR;
			break;
		}
		dev_off += bytes;
		kunmap_local(buf);
		if (is_write) up_write(&dev->lock);
		else          up_read(&dev->lock);

		if (ret != BLK_STS_OK) break;

	}

	if (is_write){
		if (atomic_dec_and_test(&dev->inflight_writes))
		{
			wake_up_all(&dev->flush_wq);
		}
	}

	out_end:
	if (atomic_cmpxchg(&ctx->done, 0, 1) == 0)
	{
		blk_mq_end_request(rq, ret);
	}
	abort:


}
static enum blk_eh_timer_return rbull_timeout(struct request *rq)
{
	struct rbull_rq_ctx *ctx = blk_mq_rq_to_pdu(rq);
	struct rbull_dev *dev = ctx->dev;
	struct rbull_hctx *rh = ctx->rh;
	atomic_set(&ctx->abort, 1);
	if (atomic_cmpxchg(&ctx->done, 0, 1) == 0)
	{
		blk_mq_end_request(rq, BLK_STS_TIMEOUT);
		printk(KERN_WARNING "rbull: timeout on request %llu on hw qid %d\n", rq->tag, rq->mq_hctx->queue_num);
	}

	return BLK_EH_DONE;

}
static int rbull_init_request(struct blk_mq_tag_set *set, struct request *rq,
							  unsigned int hctx_idx, unsigned int numa_node)
{
	struct rbull_rq_ctx *ctx = blk_mq_rq_to_pdu(rq);

	memset(ctx, 0, sizeof(*ctx));
	ctx->dev = set->driver_data;          // set this in init: tag_set.driver_data = dev
	ctx->rh = ctx->dev->hctxs[hctx_idx];
	INIT_WORK(&ctx->work, rbull_rq_work); // bind worker function
	atomic_set(&ctx->done, 0);
	atomic_set(&ctx->abort, 0);
	return 0;
}
static void rbull_exit_request(struct blk_mq_tag_set *set,
							   struct request *rq,
							   unsigned int hctx_idx)
{
	struct rbull_rq_ctx *ctx = blk_mq_rq_to_pdu(rq);
	cancel_work_sync(&ctx->work);
}

static int rbull_init_hctx(struct blk_mq_hw_ctx *hctx, void *data, unsigned int idx)
{
	struct rbull_dev *dev = data;
	struct rbull_hctx *rh = kzalloc(sizeof(*rh), GFP_KERNEL);
	rh->dev = dev;
	rh->idx = idx;
	rh->wq = alloc_ordered_workqueue("rbull_hctx%u", 0, idx); // workqueue that is 1-1 serialized
	hctx->driver_data = rh;
	dev->hctxs[idx] = rh;

	return rh->wq ? 0 : -ENOMEM;
}
static void rbull_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int idx)
{
	struct rbull_hctx *rh = hctx->driver_data;
	flush_workqueue(rh->wq);
	destroy_workqueue(rh->wq);
	kfree(rh);
}
static blk_status_t rbull_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bdq)
{
	struct request *rq = bdq->rq;
	struct rbull_dev *dev = rq->q->queuedata;
	sector_t start = blk_rq_pos(rq); // start sector
	blk_status_t ret = BLK_STS_OK;
	struct rbull_rq_ctx *ctx = blk_mq_rq_to_pdu(rq);

	/* tell the blk-mq layer that the request is in flight */
	if (blk_rq_is_passthrough(rq))
	{
		// dont support
		blk_mq_end_request(rq,  BLK_STS_IOERR);
		return BLK_STS_IOERR;

	}
	/* Fill in the ctx for dispatch async */
	ctx->dev = dev;
	ctx->rq = rq;
	ctx->dev_off = start << SECTOR_SHIFT;;
	ctx->op = req_op(rq);
	ctx->st = BLK_STS_OK;
	ctx->rh = hctx->driver_data;
	atomic_set(&ctx->done, 0);
	atomic_set(&ctx->abort, 0);
	blk_mq_start_request(rq);
	queue_work(ctx->rh->wq, &ctx->work);

	return ret;

}
struct blk_mq_ops rbull_mq_ops = {
	.queue_rq =  rbull_queue_rq,
	.init_request = rbull_init_request,
	.exit_request = rbull_exit_request,
	.init_hctx = rbull_init_hctx,
	.exit_hctx = rbull_exit_hctx,
	.timeout = rbull_timeout,
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
	init_rwsem(&dev->lock);
	atomic_set(&dev->open_count, 0);
	atomic_set(&dev->inflight_writes, 0);
	init_waitqueue_head(&dev->flush_wq);

	dev->size_bytes = (u64)disk_sectors_mb * 1024 * 1024;
	dev->capacity_sectors = dev->size_bytes >> SECTOR_SHIFT;
	dev->hctxs = kzalloc(nr_hw_queues *sizeof(struct rbull_hctx *), GFP_KERNEL);
	if (!dev->hctxs) return -ENOMEM;

	dev->data = vmalloc(dev->size_bytes);
	if (!dev->data) goto free_hctx;
	memset(dev->data, 0, dev->size_bytes);
	/* Initialize the tag set */
	memset(&dev->tag_set, 0, sizeof(struct blk_mq_tag_set));
	dev->tag_set.ops = &rbull_mq_ops;
	dev->tag_set.nr_hw_queues = nr_hw_queues; // only maps to how many cpu's are there rest are unused
	dev->tag_set.queue_depth = queue_depth; // 256 commands/requests per tag's
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.cmd_size = sizeof(struct rbull_rq_ctx); // request's private data
	dev->tag_set.nr_maps = 1; // default no polling cpu to hwctx maps
	dev->tag_set.driver_data = dev;
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
	free_hctx:
		kfree(dev->hctxs);

	return ret;
}

static void __exit rbull_exit(void)
{

	struct rbull_dev *dev = &r_dev;
	del_gendisk(dev->disk);
	put_disk(dev->disk);
	blk_mq_free_tag_set(&dev->tag_set);
	vfree(dev->data);
	kfree(dev->hctxs);
}
module_init(rbull_init)
module_exit(rbull_exit)
