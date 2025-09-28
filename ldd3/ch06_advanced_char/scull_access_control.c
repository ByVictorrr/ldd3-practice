#include <linux/fs.h>
#include <linux/container_of.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/spinlock.h>
#include <linux/fcntl.h>
#include "scull.h"
#include "scull_access_control.h"

// scullsingle - a device that only allows one open at a time - any 2nd open returns an error
static atomic_t scull_single_flag = ATOMIC_INIT(1);
struct scull_dev scull_single_dev;

static int scull_single_open(struct inode *inode, struct file *filp){
    struct scull_dev *dev = &scull_single_dev;
    if (!atomic_dec_and_test(&scull_single_flag))
    {
        // if after decrement its not zero, someone else had it open
        atomic_inc(&scull_single_flag);
        return -EBUSY;
    }
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_dev_reset(dev);
    filp->private_data = dev;
    return 0;

}

static int scull_single_release(struct inode * inode, struct file * filp)
{
    atomic_inc(&scull_single_flag);
    return 0;
}

struct file_operations scull_single_fops ={
    .owner = THIS_MODULE,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
    .read=scull_read,
    .write=scull_write,
    .open = scull_single_open,
    .release = scull_single_release,
};



// sculluid - single user at a time - allows the same user to open multiple times, but if a diff user tries while one user has it open, that open will refuse
struct scull_dev scull_uid_dev;
static DEFINE_SPINLOCK(scull_uid_lock);
uid_t scull_uid_owner;
int scull_uid_count = 0;


static int scull_uid_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev = &scull_uid_dev;
    spin_lock(&scull_uid_lock);
    if (scull_uid_count &&
        (scull_uid_owner != __kuid_val(current_uid())) &&
        (scull_uid_owner != __kuid_val(current_euid())) &&
        !capable(CAP_DAC_OVERRIDE)
        )
    {
        // device is busy with another user, and current process is not the owner nor root
        spin_unlock(&scull_uid_lock);
        return -EBUSY;
    }
    if (scull_uid_count == 0)
        scull_uid_owner =  __kuid_val(current_uid());
    scull_uid_count++;
    spin_unlock(&scull_uid_lock);
    //* then everything else is copied from bare scull */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_dev_reset(dev);
    filp->private_data = dev;
    return 0;

}

static int scull_uid_release(struct inode *inode, struct file *filp)
{
    spin_lock(&scull_uid_lock);
    scull_uid_count--;
    spin_unlock(&scull_uid_lock);
    return 0;
}
struct file_operations scull_uid_fops ={
    .owner = THIS_MODULE,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
    .read=scull_read,
    .write=scull_write,
    .open = scull_uid_open,
    .release = scull_uid_release,
};

/* scullwuid - similar to sculluid but instead of refusing a second user,
 * it will wait until the device is free (blocking open)
 */
uid_t scull_wuid_owner = (uid_t)-1;
static DEFINE_SPINLOCK(scull_wuid_lock);
static DECLARE_WAIT_QUEUE_HEAD(scull_wuid_wait);
struct scull_dev scull_wuid_dev;

int scull_wuid_count = 0;
static int scull_wuid_available(void)
{
    return scull_wuid_count == 0 ||
        scull_wuid_owner == __kuid_val(current->cred->uid) ||
        scull_wuid_owner ==__kuid_val(current->cred->euid) ||
            capable(CAP_DAC_OVERRIDE);
}
static int scull_wuid_open(struct inode *inode, struct file *filp)
{
    pr_warn("wuid_open: pid=%d euid=%u f=%#x mode=%ul openers=%d owner=%d\n",
        current->pid, __kuid_val(current_euid()),
        filp->f_flags, filp->f_mode, scull_wuid_count, scull_wuid_owner);

    struct scull_dev *dev = &scull_wuid_dev;
    spin_lock(&scull_wuid_lock);
    while (!scull_wuid_available()){
        spin_unlock(&scull_wuid_lock);
        if (filp->f_flags & O_NONBLOCK) return -EAGAIN;

        if (wait_event_interruptible(scull_wuid_wait, scull_wuid_available()))
            return -ERESTARTSYS;
        spin_lock(&scull_wuid_lock);
    }
    if (scull_wuid_count == 0)
        scull_wuid_owner =  __kuid_val(current_uid());
    scull_wuid_count++;
    spin_unlock(&scull_wuid_lock);
    /* then, everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_dev_reset(dev);
    filp->private_data = dev;
    return 0;

}

static int scull_wuid_release(struct inode *inode, struct file *filp)
{
    int temp;
    spin_lock(&scull_wuid_lock);
    scull_wuid_count--;
    temp = scull_wuid_count;
    spin_unlock(&scull_wuid_lock);
    if (temp == 0)
        wake_up_interruptible_sync(&scull_wuid_wait);
    return 0;
}
struct file_operations scull_wuid_fops ={
    .owner = THIS_MODULE,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
    .read=scull_read,
    .write=scull_write,
    .open = scull_wuid_open,
    .release = scull_wuid_release,
};

/* scullpriv: device that clones itself on open
 * Giving each opener a separate data area (keyed by something like a process id or controlling tty
 */
static LIST_HEAD(scull_priv_list);
static DEFINE_SPINLOCK(scull_priv_lock);

/* Shared structure if key matches */
struct scull_listitem{
    struct scull_dev device;
    dev_t key;
    struct list_head list;
};
static struct scull_dev *scull_find_listitem(dev_t key)
{
    struct scull_listitem *item;
    list_for_each_entry(item, &scull_priv_list, list)
    {
        if (item->key == key)
            return &(item->device);
    }
    // Here we can just create the missing key scull_dev
    item = kmalloc(sizeof(struct scull_listitem), GFP_KERNEL);
    if (!item) return NULL;
    memset(item, 0, sizeof(struct scull_listitem));
    item->key = key;
    scull_dev_reset(&item->device);
    sema_init(&item->device.sem, 1);


    list_add(&(item->list), &scull_priv_list);

    return &item->device;
}
static int scull_priv_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;
    dev_t key;
    if (!current->signal->tty)
    {
        pr_debug("Process %s has no ctl tty\n", current->comm);
        return -EINVAL;
    }
    key = tty_devnum(current->signal->tty);
    spin_lock(&scull_priv_lock);
    dev = scull_find_listitem(key);
    spin_unlock(&scull_priv_lock);

    if (!dev) return -ENOMEM;
    /* then, everything else is copied from the bare scull device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_dev_reset(dev);
    filp->private_data = dev;
    return 0;
}

static int scull_priv_release(struct inode *inode, struct file *filp)
{
    return 0;
}
struct file_operations scull_priv_fops ={
    .owner = THIS_MODULE,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
    .read=scull_read,
    .write=scull_write,
    .open = scull_priv_open,
    .release = scull_priv_release,
};
/* A placeholder scull_dev which really just holds the cdev stuff. */
static struct scull_dev scull_priv_dev;

static dev_t devno;
static struct scull_adev_info
{
    char *name;
    struct scull_dev *sculldev;
    struct file_operations *fops;
}scull_access_devs[] = {
    {"scullsingle", &scull_single_dev, &scull_single_fops},
    {"sculluid", &scull_uid_dev, &scull_uid_fops},
    {"scullwuid", &scull_wuid_dev, &scull_wuid_fops},
    {"scullpriv", &scull_priv_dev, &scull_priv_fops}
};
static struct class *cls;
static void scull_access_setup(dev_t devnum, struct scull_adev_info *devinfo)
{
    struct scull_dev *dev = devinfo->sculldev;
    int err;
    // init class
    device_create(cls, NULL, devnum, NULL, devinfo->name);
    /* Initalize the device structure */
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    sema_init(&dev->sem, 1);
    /* do the cdev stuff */
    cdev_init(&dev->cdev, devinfo->fops);
    err = cdev_add(&dev->cdev, devnum, 1);
    if (err)
    {
        pr_notice("error %d adding %s", err, devinfo->name);
        return;
    }
    pr_info("%s registered at %x", devinfo->name, devnum);


}
int scull_access_init(dev_t firstdev)
{
    int result, i;
    pr_info("sculla: devno major=%d, minor=%d", MAJOR(firstdev), MINOR(firstdev));
    cls = class_create("sculla");
    result = register_chrdev_region(firstdev, ARRAY_SIZE(scull_access_devs), "sculla");
    if (result < 0)
    {
        pr_warn("Sculla: device number registeration failed\n");
        return 0;
    }
    devno = firstdev;
    /* setup each device*/
    for (i=0; i < ARRAY_SIZE(scull_access_devs); i++)
        scull_access_setup(MKDEV(MAJOR(firstdev), MINOR(firstdev) + i) , &scull_access_devs[i]);
    return ARRAY_SIZE(scull_access_devs);

}

void scull_access_cleanup()
{
   struct scull_listitem *lptr, *next;
    int i;
    /* clean up the static devs */
    for (i=0; i < ARRAY_SIZE(scull_access_devs); i++)
    {
        struct scull_dev *dev = scull_access_devs[i].sculldev;
        cdev_del(&dev->cdev);
        scull_dev_reset(dev);
        device_destroy(cls, MKDEV(MAJOR(devno), MINOR(devno)+i));
    }
    list_for_each_entry_safe(lptr, next, &scull_priv_list, list)
    {
        list_del(&lptr->list);
        scull_dev_reset(&lptr->device);
        kfree(lptr);

    }
    /* And all the cloneed device */
    unregister_chrdev_region(devno, ARRAY_SIZE(scull_access_devs));
    class_destroy(cls);
}
