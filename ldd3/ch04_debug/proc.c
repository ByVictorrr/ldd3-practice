#include <linux/module.h>
#include <linux/proc_fs.h>
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scull_proc");
MODULE_VERSION("0.2");
struct proc_dir_entry *scull_proc_dir, *mem_file, *ctrl_file;

/* Example state kept behind the proc entries */
struct scull_state {
    u32 counter;
    char last_cmd[64];
    struct mutex lock;
} *gstate;
static ssize_t scull_mem_read(struct file *file, char __user *ubuf, size_t len, loff_t *ppos)
{
    int n;
    char buf[128];
    struct scull_state *st = pde_data(file_inode(file)); /*get gstate */
    if (!st)
        return -EINVAL;
    mutex_lock(&st->lock); /* so last_cmd and counter doesnt change by write */
    n = scnprintf(buf, sizeof(buf), "counter=%u\nlast_cmd=\"%s\"\n", st->counter, st->last_cmd);
    mutex_unlock(&st->lock);
    /* Simple one-shot read: respect *ppos so cat doesn't loop forever */
    return simple_read_from_buffer(ubuf, len, ppos, buf, n);


}
struct proc_ops scull_mem_ops = {
    .proc_read = scull_mem_read,
};
static ssize_t scull_ctrl_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
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
struct proc_ops scull_ctrl_ops = {
   .proc_write =  scull_ctrl_write,
};


int __init scull_proc_init(void)
{
    /* Allocate shared state */
    gstate = kzalloc(sizeof(*gstate), GFP_KERNEL);
    if (!gstate)
        return -ENOMEM;
    mutex_init(&gstate->lock);
    /* 1. create /proc/scull directory */
    scull_proc_dir = proc_mkdir("scull", NULL);
    if (!scull_proc_dir)
    {
        goto err_files;;

    }
    /* (Optional) change owner/size metadata if you need:
     *   proc_set_user(scull_dir, KUIDT_INIT(0), KGIDT_INIT(0));
     *   proc_set_size(scull_dir, 0);
     */

    /* 2. create read-only file(0444): /proc/scull/mem */
    mem_file = proc_create_data("mem", 0444, scull_proc_dir, &scull_mem_ops, gstate);

    if (!mem_file)
        goto err_files;;

    /* 3. create write-only file (0222) /proc/scull/ctrl */
    ctrl_file  = proc_create_data("ctrl", 0222, scull_proc_dir, &scull_ctrl_ops, gstate);
    pr_info("scull procfs ready: /proc/%s/{%s,%s}", "scull", "ctrl", "mem");
    return 0;
    err_files:
        /* remove subtree if any child creation failed */
        remove_proc_subtree("scull", NULL);
    kfree(gstate);
    return -ENOMEM;
}

static void __exit scull_proc_exit(void)
{
    /* Removes /proc/scull and all children in one shot */
    remove_proc_subtree("scull", NULL);

    kfree(gstate);
    pr_info("scull procfs removed\n");
}

module_init(scull_proc_init);
module_exit(scull_proc_exit);
