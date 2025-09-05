#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/container_of.h>
#include <linux/poll.h>
#include "scull.h"
#include "scull_ioctl.h"
#include "scull_pipe.h"

#include <linux/debugfs.h>
struct dentry *debugfs_pipe_root;

int scull_p_buffer = 100;
int scull_p_nr_devs = 4;
struct scull_pipe *scull_p_devices;
static struct class *pipe_cls;
dev_t scull_p_devno;

ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;
    // sleep if someone has the lock
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    while (dev->rp == dev->wp) // while empty
    {
        if (dev->nwriters == 0) {     // no writers and nothing to read → EOF
            up(&dev->sem);
            return 0;
        }
        /* buffer is empty */
        up(&dev->sem);

        if (filp->f_flags & O_NONBLOCK) //Nonblocking mode: no data, return immediately
            return -EAGAIN; // temporary failure, try again later

        pr_info("%s reading: going to sleep\n", current->comm);
        // wait until not empty
        if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            return -ERESTARTSYS;
        // require lock after waking up
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        // loop back and recheck condition
    }
    // Now we have data available
    /* count: will not be past dev->end */
    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev ->rp));
    else // write pointer wrapped around, read until end of buffer
        count = min(count, (size_t)(dev->end - dev->rp));

    /* copy to the user */
    if (copy_to_user(buf, dev->rp, count)){
        up(&dev->sem);
        return -EFAULT;

    }
    dev->rp += count;
    if (dev->rp == dev->end)
        dev->rp = dev->start; // * if end of buffer reset read pointer
    up(&dev->sem);
    // Wake up any writers waiting for space
    wake_up_interruptible(&dev->outq);
    pr_info("%s did read %li bytes\n", current->comm, (long int)count);
    return count;
}

static int get_spacefree(struct scull_pipe * dev)
{
    if (dev->rp == dev->wp) //emtpy
        return dev->buffersize -1; // we cant write over rp - 1 comes in
    return  (dev->rp - dev->wp + dev->buffersize) % dev->buffersize - 1;
}
int get_write_space(struct scull_pipe *dev, struct file *filp)
{
    while (get_spacefree(dev) == 0) // buffer is full
    {
        DEFINE_WAIT(wait);
        up(&dev->sem);
        if (filp->f_flags & O_NONBLOCK) // non-block return immediately
            return -EAGAIN;
        pr_info("%s writing: going to sleep\n", current->comm);
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        if (get_spacefree(dev) == 0)
            schedule();
        finish_wait(&dev->outq, &wait);

        if (signal_pending(current))
            return -ERESTARTSYS;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }
    return 0;

}
ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;
    int result;
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    /* Make sure ther is space to write */
    result = get_write_space(dev, filp);
    if (result) return result; // if error return it
    /* space is now available, buffer is locked */

    /* Determine how much to write - dont overfill the buffer */
    count = min(count, (size_t)get_spacefree(dev));
    /* Is the WP ahead of RP? That is is it near end of buffer/ wraps around */
    if (dev->wp >= dev->rp)
    {
        size_t bytes_to_end = dev->end - dev->wp;
        if (bytes_to_end > 0)
            count = min(count, (size_t)(bytes_to_end));
    }else
    {
        // writer pointer wrapper , free space is just before rp
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    }
    pr_info("Going to accept %li bytes to %p from %p\n", count, dev->wp, buf);
    if (copy_from_user(dev->wp, buf, count))
    {
        up(&dev->sem);
        return -EFAULT;
    }
    dev->wp += count;
    if (dev->wp == dev->end)
        dev->wp = dev->start; // wrap around
    up(&dev->sem);
    /* wake up any reader */
    wake_up_interruptible(&dev->inq);
    /* signal asynchronous readers, if any */
    if (dev->fasync_queue)
        kill_fasync(&dev->fasync_queue, SIGIO, POLL_IN);
    pr_info("%s did write %li bytes\n", current->comm, (long int)count);
    return count;
}

unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
    // function used by the kernel as a condition when mask != 0 to break out of wait queue

    struct scull_pipe *dev = filp->private_data;
    unsigned int mask = 0;
    down(&dev->sem);
    // register the wait queues with the poll system
    poll_wait(filp, &dev->inq, wait);
    poll_wait(filp, &dev->outq, wait);
    // at this point, if the process going to sleep, it will be on inq and outq
    // check current status:
    if (dev->rp != dev->wp) // not empty
        mask |= POLLIN | POLLRDNORM; // readable: data available
    if (get_spacefree(dev))
        mask |= POLLOUT | POLLWRNORM; // writeable: space available
    if (dev->rp == dev->wp && dev->nwriters == 0) // emtpy and no writer
        mask |= POLLHUP | POLLIN; // device is in hangup state, also mark it as readable
    up(&dev->sem);
    return mask;
}
int scull_p_fasync(int fd, struct file *filp, int on)
{
    // register/de-register on async queue to be notified when kill_async called
    struct scull_pipe *dev = filp->private_data;
    return fasync_helper(fd, filp, on, &dev->fasync_queue);
}

int scull_p_open(struct inode *inode, struct file *filp){
    struct scull_pipe *device = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = device;
    if (down_interruptible(&device->sem))
        return -ERESTARTSYS;
    if (!device->start)
    {
        // allocate the device
        device->start = kmalloc(scull_p_buffer, GFP_KERNEL);
        if (!device->start){
            up(&device->sem);
            return -ENOMEM;
        }
        device->buffersize = scull_p_buffer;
        device->end = device->start + device->buffersize;
        device->rp = device->wp = device->start;
    }
    //* use f_mode -> standarized
    if (filp->f_mode & FMODE_WRITE)
        device->nwriters++;
    if (filp->f_mode & FMODE_READ)
        device->nreaders++;
    up(&device->sem);

    return nonseekable_open(inode, filp);

}
int scull_p_release(struct inode *inode,  struct file *filp)
{
    struct scull_pipe *dev = filp->private_data;
    // remove this filp from async notified filps
    scull_p_fasync(-1, filp, 0);
    down(&dev->sem);
    if (filp->f_mode & FMODE_WRITE)
        if (--dev->nwriters == 0)
            wake_up_interruptible(&dev->inq);  // let readers see EOF
    if (filp->f_mode & FMODE_READ)
        dev->nreaders--;
    if (dev->nreaders == 0 && dev->nwriters == 0)
    {
        kfree(dev->start);
        dev->start = NULL;
    }
    up(&dev->sem);
    return 0;
}



struct file_operations scull_pipe_fops ={
    .owner = THIS_MODULE,
    .open = scull_p_open,
    .release = scull_p_release,
    .read = scull_p_read,
    .write = scull_p_write,
    .unlocked_ioctl = scull_ioctl,
    .llseek = no_llseek,
    .poll = scull_p_poll,
    .fasync = scull_p_fasync,
};

static int pipe_show(struct seq_file *m, void *v)
{
    int i;
    struct scull_pipe *p;
    #define LIMIT (PAGE_SIZE-200)        /* don't print any more after this size */
    seq_printf(m, "Default buffersize is %i\n", scull_p_buffer);
    for(i = 0; i<scull_p_nr_devs; i++) {
        p = &scull_p_devices[i];
        if (down_interruptible(&p->sem))
            return -ERESTARTSYS;
        seq_printf(m, "\nDevice %i: %p\n", i, p);
        seq_printf(m, "   Queues: %p %p\n", p->inq, p->outq);
        seq_printf(m, "   Buffer: %p to %p (%i bytes)\n", p->start, p->end, p->buffersize);
        seq_printf(m, "   rp %p   wp %p\n", p->rp, p->wp);
        seq_printf(m, "   readers %i   writers %i\n", p->nreaders, p->nwriters);
        up(&p->sem);
    }
    return 0;
}
/* Boilerplate wrapper for single_open */
static int my_open(struct inode *inode, struct file *file)
{
    return single_open(file, pipe_show, NULL);
}

static const struct file_operations debug_fs_ops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};
int scull_pipe_init(dev_t first_dev)
{
    char tmp[64];
    int i, result;
    scull_p_devno = first_dev;
    debugfs_pipe_root = debugfs_create_dir("scull_pipe", NULL);
    if (!debugfs_pipe_root)
        return -ENOMEM;
    scull_p_devices = kmalloc(scull_p_nr_devs*sizeof(struct scull_pipe), GFP_KERNEL);
    if (!scull_p_devices)
        return -ENOMEM;
    memset(scull_p_devices, 0, scull_p_nr_devs*sizeof(struct scull_pipe));
    result = register_chrdev_region(first_dev, scull_p_nr_devs, "scull_pipe");
    if (result < 0)
    {
        printk(KERN_ERR "scull_pipe: cant get char devices");
        return result;
    }
    // create class at /sys/class/scull
    pipe_cls = class_create(THIS_MODULE, "scullp");
    struct scull_pipe *p;
    for (i = 0; i < scull_p_nr_devs; i++)
    {
        // create /sys/kernel/debug/scull_pipe/pipe{i}
        dev_t devno = MKDEV(MAJOR(first_dev), MINOR(first_dev)+i);

        p = scull_p_devices + i ;
        memset(tmp, 0, sizeof(tmp));
        scnprintf(tmp, sizeof(tmp), "pipe%d", i);
        debugfs_create_file(tmp, 0644, debugfs_pipe_root, NULL, &debug_fs_ops);
        // init sem
        sema_init(&p->sem, 1);
        // setup cdev fops
        cdev_init(&p->cdev, &scull_pipe_fops);
        p->cdev.owner = THIS_MODULE;
        // register cdev with device number
        result = cdev_add(&p->cdev, devno, 1);
        if (result < 0)
        {
            printk(KERN_ERR "scull_pipe: cant add character device");
            return result;
        }
        //init wait queues
        init_waitqueue_head(&p->inq);
        init_waitqueue_head(&p->outq);
        // creates /dev/scullp{i}
        struct device *d = device_create(pipe_cls, NULL, devno, NULL, "scullp%d", i);

        if (IS_ERR(d)) {
            pr_err("device_create scullp%d failed: %ld\n", i, PTR_ERR(d));
            // (optional) unwind previously created devices
        }
    }
    return scull_p_nr_devs;
}
void scull_pipe_exit(void)
{
    int i;
    dev_t minor = MINOR(scull_p_devno);
    // remove /sys/kernel/debug/scull_pipe
    debugfs_remove_recursive(debugfs_pipe_root);

    struct scull_pipe *p;
    if (scull_p_devices)
    {

        for (i=0; i < scull_p_nr_devs; i++)
        {
            p = scull_p_devices + i;
            // unregister file ops from cdev
            cdev_del(&p->cdev);
            device_destroy(pipe_cls,MKDEV(MAJOR(scull_p_devno), minor+i));
            kfree(p->start);
        }
    }
    // remove char region numbers
    unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
    kfree(scull_p_devices);
    scull_p_devices = NULL;
    class_destroy(pipe_cls);

}