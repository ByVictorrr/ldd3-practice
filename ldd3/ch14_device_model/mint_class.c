#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>

#include "mint.h"


static dev_t mint_devno;
static struct cdev mint_cdev;
static struct device *selected_dev;

static ssize_t mint_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    char name[MAX_MINT_ID_LEN];
    struct device *temp = NULL;
    ssize_t len;
    if (count >= MAX_MINT_ID_LEN)
        count = MAX_MINT_ID_LEN - 1;
    if (copy_from_user(name, buf, count)) return -EFAULT;
    name[count] = '\0';
    len = strcspn(name, "\n");
    name[len] = '\0'; // stip new line


    /* optional: check if already exists */
    temp = bus_find_device_by_name(&mint_bus, NULL, name);
    if (!selected_dev && !temp) return -ENODEV;
    // here you can set private data for selected device
    if (temp)
    {
        selected_dev = temp;
        pr_info("mint_class: selected %s\n", temp->init_name);
    }
    else if (selected_dev && !temp)
    {
        struct mint_dev *mdev = to_mint_device(selected_dev);
        if (mdev->priv_data) kfree(mdev->priv_data);
        mdev->priv_data = kstrdup(name, GFP_KERNEL);
        if (!mdev->priv_data) return -ENOMEM;
        pr_info("mint_class: copied %s to device %s\n", name, mdev->id.name);
    }

    return count;
}

static ssize_t mint_read(struct file *filp, char __user *buf,
                         size_t count, loff_t *f_pos)
{
    struct mint_dev *mdev;

    if (!selected_dev)
        return 0;

    mdev = to_mint_device(selected_dev);
    if (!mdev || !mdev->priv_data)
        return 0;

    return simple_read_from_buffer(buf,
                                   count,
                                   f_pos,
                                   mdev->priv_data,
                                   strlen(mdev->priv_data));
}

struct file_operations mint_fops = {
    .write = mint_write,
    .read = mint_read,
    .open = simple_open,
    .owner = THIS_MODULE
};

static int dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    // Add class specific uevent env vars for device events
    // called from device_uevent which is default ops for kset for subsystem
    // for pci it would be the pci.ids
    // for KOBj_ADD its called in device_add
    struct mint_dev *mdev = to_mint_device(dev);
    add_uevent_var(env, "SUBSYSTEM=mint_class");
    add_uevent_var(env, "DEV_NAME=%s", dev_name(dev));
    return 0;

}
static int __init mintclass_init(void){
    int ret = alloc_chrdev_region(&mint_devno, 0, 1, "mint");
    // mint_class->class_groups =
    // mint_class->dev_groups
    mint_class->dev_uevent = dev_uevent;
    if (ret) return ret;
    cdev_init(&mint_cdev, &mint_fops);
    cdev_add(&mint_cdev, mint_devno, 1);
    selected_dev = NULL;
    /* parent=NULL => shows under /sys/devices/virtual/mint/mint0 and
       creates /sys/class/mint/mint0 symlink; uevent -> /dev/mint0 */
    device_create(mint_class, NULL, mint_devno, NULL, "mint0");
    return 0;
}
static void __exit mintclass_exit(void){
    device_destroy(mint_class, mint_devno);
    cdev_del(&mint_cdev);
    unregister_chrdev_region(mint_devno, 1);
}
module_init(mintclass_init);
module_exit(mintclass_exit);
MODULE_LICENSE("GPL");