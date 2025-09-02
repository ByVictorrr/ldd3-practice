#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/container_of.h>
#include <linux/poll.h>
#include "scull.h"
#include "scull_ioctl.h"
#include "scull_pipe.h"



ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_pipe *dev = filp->private_data;
    // sleep if someone has the lock
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    while (dev->rp == dev->wp)
    {
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
    if (dev->rp == dev->end)
        return dev->buffersize -1; // we cant write over rp - 1 comes in
    return  ((dev->rp - dev->wp + dev->buffersize) % dev->buffersize) - 1;
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
    struct scull_pipe *dev = filp->private_data;
    unsigned int mask = 0;
    // register the wait queues with the poll system
    poll_wait(filp, &dev->inq, wait);
    poll_wait(filp, &dev->outq, wait);
    // at this point, if the process going to sleep, it will be on inq and outq
    // check current status:
    if (dev->rp != dev->wp) // not empty
        mask |= POLLIN | POLLRDNORM; // readable: data available
    if (get_spacefree(dev))
        mask |= POLLOUT | POLLWRNORM; // writeable: space available
    return mask;
}
int scull_p_open(struct inode *inode, struct file *filp){
    struct scull_pipe *device = container_of(inode->i_cdev, struct scull_pipe, cdev);

}
int scull_p_release(struct inode *inode,  struct file *filp){}



struct file_operations scull_pipe_fops ={
    .owner = THIS_MODULE,
    .open = scull_p_open,
    .release = scull_p_release,
    .read = scull_p_read,
    .write = scull_p_write,
    .unlocked_ioctl = scull_ioctl,
    .llseek = no_llseek,
    .poll = scull_p_poll,
};