// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");
MODULE_DESCRIPTION("scull_sysfs");
MODULE_VERSION("0.1");

struct scull_state {
    u32  counter;
    char last_cmd[64];
    struct mutex lock;
} *gstate;

static struct kobject *scull_kobj;

static ssize_t counter_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", gstate->counter);
}
static ssize_t counter_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
   unsigned int val;
    /* try to convert buf into uint  using base 10 */
    if (kstrtouint(buf, 10, &val))
        return -EINVAL;
    mutex_lock(&gstate->lock);
    gstate->counter = val;
    mutex_unlock(&gstate->lock);
    return count; /* bytes consumed */

}


/* Registers kobj_attribute with name, permission, show callback, and store callback */
static struct kobj_attribute counter_attr = __ATTR(counter, 0664, counter_show, counter_store);

static int __init scull_sysfs_init(void)
{
    gstate = kzalloc(sizeof(struct scull_state), GFP_KERNEL);
    if (!gstate)
        return -ENOMEM;

    mutex_init(&gstate->lock);
    gstate->counter = 0;
    strscpy(gstate->last_cmd, "", sizeof(gstate->last_cmd));

    /* create /sys/kernel/scull/ directory; kernel_kobj represents /sys/kernel/ directory */
    scull_kobj = kobject_create_and_add("scull", kernel_kobj);
    if (!scull_kobj)
    {
        kfree(gstate);
        return -ENOMEM;
    }
    /* Create the file /sys/kernel/scull/counter */
    if (sysfs_create_file(kernel_kobj, &counter_attr.attr)) {
        pr_err("scull: failed to create sysfs file\n");
        kobject_put(scull_kobj); // try to remove directory after ref count == 0
        kfree(gstate);
        return -ENOMEM;
    }

    pr_info("scull: sysfs /sys/kernell/scull/counter ready\n");
    return 0;
}

static void __exit scull_sysfs_exit(void)
{
    sysfs_remove_file(scull_kobj, &counter_attr.attr); /* remove /sys/kernel/scull/counter*/
    kobject_put(scull_kobj);  // try to remove directory after ref count == 0
    kfree(gstate);
    pr_info("scull_sysfs: removed\n");
}

module_init(scull_sysfs_init);
module_exit(scull_sysfs_exit);
