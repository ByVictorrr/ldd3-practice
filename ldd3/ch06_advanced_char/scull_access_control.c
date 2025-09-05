#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/container_of.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include "scull.h"
#include "scull_ioctl.h"
#include "scull_pipe.h"

// scullsingle - a device that only allows one open at a time - any 2nd open returns an error
atomic_t scull_single_flag;
struct scull_dev scull_single_dev;

int scull_single_open(struct inode *inode, struct file *filp){
    struct scull_dev *dev = &scull_single_dev;
    if (atomic_dec_and_test(&scull_single_flag))
    {
        // if after decrement its not zero, someone else had it open
        atomic_inc(&scull_single_flag);
        return -EBUSY;
    }
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scull_dev_reset(dev);
        up(&dev->sem);
    return 0;

}

int scull_single_release(struct inode * inode, struct file * filp)
{
   atomic_inc(&scull_single_flag);
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
spinlock_t scull_uid_lock;
uid_t scull_uid_owner;
int scull_uid_count = 0;


int scull_uid_open(struct inode *inode, struct file *filp)
{
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
    return 0;

}

int scull_uid_release(struct inode *inode, struct file *filp)
{
    spin_lock(&scull_uid_lock);
    if (scull_uid_count)
        scull_uid_count--;
    spin_unlock(&scull_uid_lock);
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

// scullwuid - similar to sculuid but instead of refusing a second user, it will wait until the device is free (blocking open)

// scullpriv - device that clones itself on open , giving each opener a separe data area (keyed by something like a process id or controlling tty
