// kro.c - read-only misc char device: /dev/kro
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define KRO_MAX 128

/* ---------------- Random string helper ---------------- */

static void generate_random_string(char *buf, size_t len)
{
	static const char charset[] =
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	const size_t n = sizeof(charset) - 1;
	size_t i;
	u8 rb;

	if (!len)
		return;

	for (i = 0; i < len - 1; i++) {
		get_random_bytes(&rb, 1);
		buf[i] = charset[rb % n];
	}
	buf[len - 1] = '\0';
}

/* ---------------- Node + head list ---------------- */

struct ktypes_buf {
	char buf[KRO_MAX];
	size_t buflen;
	struct list_head node;
};

/* Global head list and lock */
static LIST_HEAD(kro_head_list);
static DEFINE_SPINLOCK(kro_list_lock);

/*
 * kro_alloc_node()
 *  - Returns a valid node pointer on success
 *  - Returns ERR_PTR(-ENOMEM) on allocation failure
 */
static struct ktypes_buf *kro_alloc_node(void)
{
	struct ktypes_buf *n = kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return ERR_PTR(-ENOMEM);

	generate_random_string(n->buf, sizeof(n->buf));
	n->buflen = strnlen(n->buf, sizeof(n->buf));
	INIT_LIST_HEAD(&n->node);
	return n;
}

/* Push an already-initialized node to the head (LIFO). Never fails. */
static void kro_list_push_head_node(struct ktypes_buf *n)
{
	spin_lock(&kro_list_lock);
	list_add(&n->node, &kro_head_list);
	spin_unlock(&kro_list_lock);
}

/*
 * kro_list_pop_head_node()
 *  - Returns pointer to node on success
 *  - Returns ERR_PTR(-ENOENT) if list is empty
 */
static struct ktypes_buf *kro_list_pop_head_node(void)
{
	struct ktypes_buf *n;

	spin_lock(&kro_list_lock);
	if (list_empty(&kro_head_list)) {
		spin_unlock(&kro_list_lock);
		return ERR_PTR(-ENOENT);
	}
	n = list_first_entry(&kro_head_list, struct ktypes_buf, node);
	list_del(&n->node);
	spin_unlock(&kro_list_lock);

	return n;
}

/* Clear entire list (free nodes). */
static void kro_list_clear_all(void)
{
	struct ktypes_buf *pos, *tmp;

	spin_lock(&kro_list_lock);
	list_for_each_entry_safe(pos, tmp, &kro_head_list, node) {
		list_del(&pos->node);
		kfree(pos);
	}
	spin_unlock(&kro_list_lock);
}

/* ---------------- File operations ---------------- */

static ssize_t kro_read(struct file *f, char __user *ubuf, size_t len, loff_t *ppos)
{
	struct ktypes_buf *n;
	ssize_t out;

	/* Try to pop an existing node first; if none, allocate a fresh one. */
	n = kro_list_pop_head_node();
	if (IS_ERR(n)) {
		if (PTR_ERR(n) != -ENOENT)
			return PTR_ERR(n);          /* unexpected list error */

		/* List empty: create a new node (may return ERR_PTR). */
		n = kro_alloc_node();
		if (IS_ERR(n))
			return PTR_ERR(n);          /* -ENOMEM */
	}

	out = min_t(ssize_t, n->buflen, len);
	if (copy_to_user(ubuf, n->buf, out)) {
		kfree(n);
		return -EFAULT;
	}

	/* We consumed the node for this read; free it. */
	kfree(n);
	return out;
}

static const struct file_operations kro_fops = {
	.owner  = THIS_MODULE,
	.read   = kro_read,
	.write  = NULL,
	.llseek = noop_llseek,
};

/* ---------------- Device + module ---------------- */

static struct miscdevice kro_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "kro",
	.fops  = &kro_fops,
	.mode  = 0444, /* read-only */
};

static int __init kro_init(void)
{
	int r = misc_register(&kro_dev);
	if (!r)
		pr_info("kro loaded\n");
	else
		pr_err("kro register failed: %d\n", r);
	return r;
}

static void __exit kro_exit(void)
{
	kro_list_clear_all();
	misc_deregister(&kro_dev);
	pr_info("kro unloaded\n");
}

module_init(kro_init);
module_exit(kro_exit);

MODULE_DESCRIPTION("kro: read-only misc device demonstrating head list + ERR_PTR pattern");
MODULE_AUTHOR("you");
MODULE_LICENSE("GPL");
