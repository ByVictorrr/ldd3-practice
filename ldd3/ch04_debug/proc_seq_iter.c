// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("seq_file over a kernel hlist");
MODULE_VERSION("0.1");

/* -------- Data node stored in a hlist -------- */
struct scull_item {
    struct hlist_node node;  /* hlist hook */
    u32  id;
    u64  value;
    char name[32];
};

/* The hlist head and its lock */
static HLIST_HEAD(scull_hlist);
static DEFINE_MUTEX(scull_hlist_lock);

/* /proc entry */
static struct proc_dir_entry *hlist_pde;

static void *scull_hlist_start(struct seq_file *m, loff_t *pos)
{
    int n;
    struct scull_item *item;
    hlist_for_each_entry(item, &scull_hlist, node)
    {
        if (n++ == *pos)
            return item;
    }
    return NULL;
}

static void *scull_hlist_next(struct seq_file *m, void *v, loff_t *pos)
{
    struct scull_item *item = v;
    (*pos)++;
    if (!item->node.next) /* end of the list */
        return NULL;
    return hlist_entry(item->node.next, struct scull_item, node);

}
static void scull_hlist_stop(struct seq_file *m, void *v)
{
    mutex_unlock(&scull_hlist_lock);

}
static int scull_hlist_show(struct seq_file *m, void *v)
{
    struct scull_item *item = v;
    seq_printf(m, "id=%u value=%llu name=%s\n", item->id, item->value, item->name);
    return 0;
}
struct seq_operations scull_seq_iter_ops = {
    .start=scull_hlist_start,
     .next= scull_hlist_next,
     .stop=scull_hlist_stop,
     .show=scull_hlist_show
};


static int scull_hlist_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &scull_seq_iter_ops);
}


static const struct proc_ops scull_proc_ops = {
    .proc_open    = scull_hlist_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = seq_release,
};
int __init scull_seq_iter_init(void)
{
    size_t i;
    /* Populate a few demo items */
    for (i = 0; i < 5; i++)
    {
        struct scull_item *item = kzalloc(sizeof(*item), GFP_KERNEL);
        if (!item)
            return -ENOMEM;
        item->id = i;
        item->value = 2000ULL + 3ULL * i;
        snprintf(item->name, sizeof(item->name), "hitem%zu", i);
        mutex_lock(&scull_hlist_lock);
        hlist_add_head(&item->node, &scull_hlist);
        mutex_unlock(&scull_hlist_lock);

    }

    hlist_pde = proc_create("scull_hlist", 0444, NULL, &scull_proc_ops);

    if (!hlist_pde)
        return -ENOMEM;

    pr_info("scull_hlist procfs ready: /proc/scull_hlist");
    return 0;
}

static void __exit scull_seq_iter_exit(void)
{
    struct scull_item *item;
    struct hlist_node *node;
    remove_proc_entry("scull_hlist", NULL);
    hlist_for_each_entry_safe(item, node, &scull_hlist, node)
    {
        /* Free the list */
        mutex_lock(&scull_hlist_lock);
        hlist_del(&item->node);
        kfree(item);

    }
    mutex_unlock(&scull_hlist_lock);

    pr_info("scull_hlist: removed\n");
}

module_init(scull_seq_iter_init);
module_exit(scull_seq_iter_exit);
