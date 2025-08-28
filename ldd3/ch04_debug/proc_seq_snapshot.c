// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("scull_state -> /proc using seq_file single_open()");
MODULE_VERSION("0.1");

struct scull_state {
    u32  counter;
    char last_cmd[64];
    struct mutex lock;
} *gstate;

static struct proc_dir_entry *pde;

/* ---- seq_file show(): dump everything once per open ---- */
static int scullmem_show(struct seq_file *m, void *v)
{
    struct scull_state *st = m->private;

    if (mutex_lock_interruptible(&st->lock))
        return -ERESTARTSYS;

    seq_printf(m, "counter=%u\nlast_cmd=\"%s\"\n", st->counter, st->last_cmd);

    mutex_unlock(&st->lock);
    return 0;
}

static int scullmem_open(struct inode *inode, struct file *file)
{
    /* single_open stores seq_file at file->private_data; m->private = gstate */
    int ret = single_open(file, scullmem_show, pde_data(inode));
    return ret;
}
static ssize_t scullmem_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
    struct scull_state *st = pde_data(file_inode(file));
    if (!st)
        return -EINVAL;
    char tmp[sizeof(st->last_cmd)];
    size_t n  = min(len, sizeof(tmp) - 1);
    if (copy_from_user(tmp, ubuf, n))
        return -EFAULT;
    tmp[n] = '\0';

    mutex_lock(&st->lock);
    if (sysfs_streq(tmp, st->last_cmd))
    {
        st->counter++;
        strscpy(st->last_cmd, "inc", sizeof(tmp));

    }else
    {
        strreplace(tmp, '\n', '\0');
        strscpy(st->last_cmd, tmp, sizeof(tmp));
    }

    mutex_unlock(&st->lock);
    return n;
}
static const struct proc_ops scullmem_ops = {
    .proc_open    = scullmem_open,
    .proc_write = scullmem_write,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ---- module init/exit ---- */
static int __init scull_sysfs_init(void)
{
    gstate = kzalloc(sizeof(*gstate), GFP_KERNEL);
    if (!gstate)
        return -ENOMEM;

    mutex_init(&gstate->lock);
    gstate->counter = 0;
    strscpy(gstate->last_cmd, "", sizeof(gstate->last_cmd));

    /* Single read-only proc node at /proc/scull_mem, with gstate as node data */
    pde = proc_create_data("scull_mem", 0666, NULL, &scullmem_ops, gstate);
    if (!pde) {
        kfree(gstate);
        return -ENOMEM;
    }

    pr_info("scull_mem: /proc/scull_mem ready\n");
    return 0;
}

static void __exit scull_sysfs_exit(void)
{
    remove_proc_entry("scull_mem", NULL);
    kfree(gstate);
    pr_info("scull_mem: removed\n");
}

module_init(scull_sysfs_init);
module_exit(scull_sysfs_exit);
