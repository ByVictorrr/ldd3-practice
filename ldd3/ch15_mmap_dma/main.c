#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kfifo.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include "edu.h"
#include "edu_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION("EDU PCI Driver - learning in chapter 15 (mmap/GUP/dma)");


static int ms_wait_for_dma = 500;
module_param(ms_wait_for_dma, int, 0);
MODULE_PARM_DESC(ms_wait_for_dma, "Waiting for DMA transfer");

static bool interrupts_enabled = true;
module_param(interrupts_enabled, bool, 0);
MODULE_PARM_DESC(interrupts_enabled, "Enable interrupts");


struct miscdevice miscdev;

static int edu_open(struct inode *inode, struct file *filp)
{
	struct edu_dev *dev = dev_get_drvdata(miscdev.this_device);
	filp->private_data = dev;
	dev->ms_wait_for_dma = ms_wait_for_dma;
	dev->interrupts_enabled = interrupts_enabled;

	return 0;
}

static int edu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/* You want to map the physical pages backing dev->dma_buf into userspace.
		That means:
			Get PFN(s) from dev->dma_handle (it’s a DMA/bus address).
			Use remap_pfn_range().
	 */
	struct edu_dev *dev = filp->private_data;
	unsigned long pfn;
	if (vma->vm_pgoff == 0)
	{
		/** map the coherent dma buffer into userspace*/
		return dma_mmap_coherent(&dev->pdev->dev, vma,
			dev->dma_buf, dev->dma_handle, DMA_BUF_SIZE);
	}


	if (vma->vm_pgoff == 1)
	{
		// map edu on-device dma register
		resource_size_t phys = pci_resource_start(dev->pdev, 0) + EDU_BAR0_DMA_BUFFER_REG;
		pfn = (unsigned long)phys >> PAGE_SHIFT;


		// Note: we are creating new pte's in side the users pgd to map to our physical page
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		// this is io mem region that cant be remmap, not included in coredump, and not copied when forked
		vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY| VM_PFNMAP);
		return remap_pfn_range(vma, vma->vm_start, pfn, DMA_BUF_SIZE, vma->vm_page_prot);
	}



	return -EINVAL;
}
static long do_sg_dma(struct edu_dev * dev, struct edu_stream_desc *sg_desc)
{
	/*
	 * do_sg_dma()
	 *
	 * NOTE: The EDU hardware only has a single 4KiB DMA buffer at
	 * EDU_BAR0_DMA_BUFFER_REG. There is no device-side notion of a
	 * "descriptor ring" or streaming FIFO.
	 *
	 * This function therefore only implements *host-side* scatter/gather:
	 *
	 *   - It pins user pages and builds a scatterlist.
	 *   - It then issues one DMA per SG segment to/from the single
	 *     4KiB device buffer.
	 *
	 * As a result:
	 *   - It is meaningful for copying between an arbitrary user buffer
	 *     and the EDU's 4KiB buffer.
	 *   - It is NOT a full streaming SG engine; a TX followed by an RX
	 *     using this API does not produce a 1:1 "loopback" of a larger
	 *     buffer, because each DMA always targets the same device window.
	 */

	int ret = 0;
	unsigned long start = sg_desc->user_addr;
	unsigned long first_page = start & PAGE_MASK;
	unsigned long offset = start & ~PAGE_MASK;
	unsigned long last_page = (start + sg_desc->length - 1) & PAGE_MASK;
	int i;
	// PAGE SHIFT ELIMIATES OFFSET BYTES
	unsigned long nr_pages = ((last_page - first_page) >> PAGE_SHIFT) + 1;
	unsigned int gup_flags = 0;
	enum dma_data_direction dma_dir = (sg_desc->dir == EDU_DMA_DIR_RAM_TO_DEV)
				? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	struct page **pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (dma_dir == DMA_FROM_DEVICE)
		gup_flags |= FOLL_WRITE;

	if (!pages) return -ENOMEM;
	// mapping into kernel space - create pte with writable, also it will be pinned for a while avoid mm
	// long pinned = pin_user_pages(first_page, nr_pages, FOLL_WRITE | FOLL_LONGTERM, pages);
	// fast_path: note this will not work if user pages not paged in already
	long pinned = pin_user_pages_fast(first_page, nr_pages, gup_flags , pages);
	if (pinned == nr_pages)
		goto have_pages;
		//unpin what suceeded
	if (pinned > 0)
	{
		for (i=0; i < pinned; i++)
			unpin_user_page(pages[i]);

	}
	/* use the slow GUP with faulting*/
	pinned = pin_user_pages(first_page, nr_pages, gup_flags, pages);
	if (pinned < 0)
	{
		ret = pinned;
		goto err_free_pages;
	}
	if (pinned < nr_pages)
	{
		ret = -EFAULT;
		goto err_unpin;
	}
	have_pages:
	// now pages[0..pinned-1] are pinned
	struct scatterlist *sg = kmalloc_array(pinned, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
	{
		ret = -ENOMEM;
		goto err_unpin;
	}
	sg_init_table(sg, pinned);
	size_t remaining = sg_desc->length;
	for (i = 0; i < pinned && remaining; i++)
	{
		size_t len = min_t(size_t, PAGE_SIZE - offset, remaining);
		sg_set_page(sg + i, pages[i], len, offset);
		offset = 0;
		remaining-= len;
	}
	if (remaining) {
		pr_err("edu: do_sg_dma: remaining=%zu after SG build (BUG)\n", remaining);
		ret = -EFAULT;
		goto err_free_sg;
	}

	int mnts = i;
	mnts = dma_map_sg(&dev->pdev->dev, sg, mnts, dma_dir);
	// mnts no necessarily equal to pin scatterlist merges continuious pages
	if (mnts <= 0)
	{
		ret = -ENOMEM;
		goto err_free_sg;
	}
	// dma fields are filled of sg
	struct scatterlist *s;
	for_each_sg(sg, s, mnts, i)
	{
		dma_addr_t dma_addr = sg_dma_address(s);
		long len = sg_dma_len(s);
		/*
		 * Usually here we would set the descriptor ring in mmio - hw_desc
		 *  hw_desc[i].addr = dma_addr
		 *  hw_desc[i].length = len
		 *  Then after all have been set ring the bell
		 */
		ret = edu_dma_transfers(dev, dma_addr, sg_desc->dir, len);
		if (ret) break;

	}
	dma_unmap_sg(&dev->pdev->dev, sg, mnts, dma_dir);
	kfree(sg);
	/* release page references */
	unpin_user_pages(pages, pinned);
	kfree(pages);
	return ret;
	err_free_sg:
		kfree(sg);
	err_unpin:
	/* Drop the page refs we got from get_user_pages() */
		if (pinned > 0)
			unpin_user_pages(pages, pinned);
	err_free_pages:
		kfree(pages);
	return ret;


}
static long edu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Create ioctl so the user can:
	 * 1. trigger a TX DMA
	 * 2. trigger a RX DMA
	 */
	struct edu_dev *dev = filp->private_data;
	long ret;
	int len;
	struct edu_stream_desc desc;
	pr_info("edu: ioctl cmd=0x%x (_IOC_NR=%u, magic=%c)\n",
		cmd, _IOC_NR(cmd), _IOC_TYPE(cmd));


	ret = pm_runtime_resume_and_get(&dev->pdev->dev);
	if (ret < 0) return ret;
	switch (cmd)
	{
		case EDU_IOC_STREAM_DMA:
			if (copy_from_user(&desc, (unsigned __user *)arg, sizeof(desc)))
			{
				ret = -EFAULT;
				break;
			}
			ret = do_sg_dma(dev, &desc);
			break;

		case EDU_IOC_DMA_TX:
			// read from ram for the device
			if (copy_from_user(&len, (unsigned __user *)arg, sizeof(len)))
			{
				ret = -EFAULT;
				break;
			}
			ret = edu_dma_transfers(dev, dev->dma_handle, EDU_DMA_DIR_RAM_TO_DEV, len);
			break;
		case EDU_IOC_DMA_RX:
			if (copy_from_user(&len, (unsigned __user *)arg, sizeof(len)))
			{
				ret = -EFAULT;
				break;
			}
			ret = edu_dma_transfers(dev, dev->dma_handle, EDU_DMA_DIR_DEV_TO_RAM, len);

			break;

		default:
			ret = -ENOTTY;
	}
	pm_runtime_mark_last_busy(&dev->pdev->dev);
	pm_runtime_put_autosuspend(&dev->pdev->dev);
	return ret;
}

static const struct file_operations uac_fops = {
	.owner  = THIS_MODULE,
	.open   = edu_open,
	.read   = edu_read,
	.write  = edu_write,
	.llseek = noop_llseek,
	.mmap   = edu_mmap, // new
	.unlocked_ioctl = edu_ioctl, // new
};



struct miscdevice miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "edu",
	.fops  = &uac_fops,
	.mode  = 0660,
};

const struct pci_device_id edu_ids[] = {
	{PCI_DEVICE(EDU_VENDOR, EDU_DEVICE)},
	{0,},
};


MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_drv = {
	.name = DRIVER_NAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,
	.err_handler = &edu_err_handlers,
	.driver = {
		.pm = &edu_pm_ops

	},

};

static int __init edu_init(void)
{
	return pci_register_driver(&edu_drv);
}
static void __exit edu_exit(void)
{
	pci_unregister_driver(&edu_drv);
}

module_init(edu_init);
module_exit(edu_exit);

//module_pci_driver(edu_drv);

