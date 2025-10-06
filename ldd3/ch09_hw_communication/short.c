// short.c — one /dev/short; select DATA/STATUS/CTRL by file position (0..2)
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_VERSION("0.3");
MODULE_DESCRIPTION("short - port I/O & MMIO demo (DATA/STATUS/CTRL via llseek)");

#define SPAN 3  /* offsets: 0=DATA, 1=STATUS (RO), 2=CTRL */

/* Parameters */
static unsigned long short_base = 0x378;     /* port base OR MMIO phys base */
module_param(short_base, ulong, 0444);
MODULE_PARM_DESC(short_base, "Base address: I/O port base (e.g. 0x378) or MMIO phys base");

static bool use_mem;                          /* 0=port I/O, 1=MMIO */
module_param(use_mem, bool, 0444);
MODULE_PARM_DESC(use_mem, "Use MMIO (true) or port I/O (false, default)");

/* State */
static void __iomem *mmio_base;               /* non-NULL only when use_mem=true */

static inline bool reg_ok(loff_t pos) { return pos >= 0 && pos < SPAN; }

static loff_t short_llseek(struct file *f, loff_t off, int whence)
{
	loff_t newpos;

	switch (whence) {
	case SEEK_SET: newpos = off; break;
	case SEEK_CUR: newpos = f->f_pos + off; break;
	case SEEK_END: newpos = SPAN + off; break; /* allow seeking to exact [0..3) */
	default: return -EINVAL;
	}
	if (!reg_ok(newpos))
		return -EINVAL;
	f->f_pos = newpos;
	return newpos;
}

static ssize_t short_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	size_t i;
	u8 v;

	if (!count)
		return 0;
	if (!reg_ok(*f_pos))
		return -EINVAL;

	for (i = 0; i < count; i++) {
		if (use_mem)
			v = ioread8(mmio_base + *f_pos);
		else
			v = inb(short_base + *f_pos);

		if (copy_to_user(buf + i, &v, 1))
			return i ? (ssize_t)i : -EFAULT;
	}
	return count;
}

static ssize_t short_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	size_t i;
	u8 v;

	if (!count)
		return 0;
	if (!reg_ok(*f_pos))
		return -EINVAL;

	/* STATUS register (offset 1) is read-only */
	if (*f_pos == 1)
		return -EPERM;

	for (i = 0; i < count; i++) {
		if (copy_from_user(&v, buf + i, 1))
			return i ? (ssize_t)i : -EFAULT;

		if (use_mem)
			iowrite8(v, mmio_base + *f_pos);
		else
			outb(v, short_base + *f_pos);
	}
	return count;
}

static const struct file_operations short_fops = {
	.owner  = THIS_MODULE,
	.read   = short_read,
	.write  = short_write,
	.llseek = short_llseek,
};

static struct miscdevice short_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "short",
	.fops  = &short_fops,
	.mode  = 0660,
};

static int __init short_init(void)
{
	int ret;

	if (use_mem) {
		/* MMIO: reserve physical range, map to kernel VA */
		if (!request_mem_region(short_base, SPAN, "short-mmio"))
			return -EBUSY;

		mmio_base = ioremap(short_base, SPAN);
		if (!mmio_base) {
			release_mem_region(short_base, SPAN);
			return -ENOMEM;
		}
	} else {
		/* Port I/O: reserve ports; no mapping */
		if (!request_region(short_base, SPAN, "short-io"))
			return -EBUSY;
	}

	ret = misc_register(&short_device);
	if (ret) {
		if (use_mem) {
			iounmap(mmio_base);
			release_mem_region(short_base, SPAN);
			mmio_base = NULL;
		} else {
			release_region(short_base, SPAN);
		}
		return ret;
	}

	pr_info("short: %s at 0x%lx (len=%d) as /dev/short; select reg with lseek(0..2)\n",
		use_mem ? "MMIO" : "port I/O", short_base, SPAN);
	return 0;
}

static void __exit short_exit(void)
{
	misc_deregister(&short_device);

	if (use_mem) {
		iounmap(mmio_base);
		release_mem_region(short_base, SPAN);
		mmio_base = NULL;
	} else {
		release_region(short_base, SPAN);
	}
}

module_init(short_init);
module_exit(short_exit);
