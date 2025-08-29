#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scull_debugfs");
MODULE_VERSION("0.2");
struct dentry *debugfs_root;

/* Example state kept behind the proc entries */
u32 counter;
char last_cmd[64];
static DEFINE_MUTEX(scull_lock);

static ssize_t scull_lastcmd_read(struct file *file, char __user *ubuf, size_t len, loff_t *ppos)
{

    char tmp[64];
    size_t n;
    mutex_lock(&scull_lock);
    n = snprintf(tmp, sizeof(last_cmd), "%s\n", last_cmd);
    mutex_unlock(&scull_lock);
    return simple_read_from_buffer(ubuf, len, ppos, tmp, n);
}


static ssize_t scull_lastcmd_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
    char tmp[64];
    size_t n = min_t(size_t, len, sizeof(tmp));

    if (copy_from_user(tmp, ubuf, n))
        return -EFAULT;
    if (tmp[n-1] == '\n')
        n--;
    tmp[n] = '\0';

    mutex_lock(&scull_lock);
    memcpy(last_cmd, tmp, n+1);
    counter++;
    mutex_unlock(&scull_lock);
    *ppos = 0;
    return len;
}

struct file_operations scull_debugfs_ops= {
    .read=scull_lastcmd_read,
    .write=scull_lastcmd_write,
};
int __init scull_debugfs_init(void)
{
    /* Allocate shared state */
    debugfs_root = debugfs_create_dir("scull", NULL);
    if (!debugfs_root)
        return -ENOMEM;
    memset(last_cmd, '\0', sizeof(last_cmd));
    counter = 0;
    debugfs_create_u32("counter", 0644, debugfs_root, &counter);
    debugfs_create_file("last_cmd", 0644, debugfs_root, NULL, &scull_debugfs_ops);
    pr_info("scull: /sys/kernel/debug/scull/{counter,last_cmd} created");
    return 0;
}

static void __exit scull_debugfs_exit(void){debugfs_remove_recursive(debugfs_root);}

module_init(scull_debugfs_init);
module_exit(scull_debugfs_exit);
