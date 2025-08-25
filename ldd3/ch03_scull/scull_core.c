#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/container_of.h>
#include "scull.h"


int scull_dev_reset(struct scull_dev *dev)
{
    struct scull_qset *next, *curr;
    int i;
    /* Loop around all qsets */
    for (curr=dev->data; curr; curr=next)
    {
        if (curr->data){
            for (i=0; dev->qset; i++)
            {
                /* Free the ith Quantum */
                if (curr->data[i])
                {
                    kfree(curr->data[i]);
                    curr->data[i] = NULL;
                }
            }
            next=curr->next;
            kfree(curr);
        }

    }
    dev->size=0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *device = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = device; // to be used in other callbacks
    /* Special case if opened for write only: reset device */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_dev_reset(device);
    return 0;
}
int scull_release(struct inode *inode,  struct file *filp){return 0;}

struct scull_qset *scull_find_item(struct scull_dev *dev, int item)
{
    int n = item;
    /* When the first node in ll is NULL */
    if (!dev->data)
        dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL );
        if (!dev->data) return NULL;

    struct scull_qset *curr = dev->data;
    while (--n > 0)
    {
        if (!curr)
        {
            curr = kmalloc(sizeof(struct scull_qset), GFP_KERNEL );
            if (!curr) return NULL;
        }
        curr = curr->next;

    }
    return curr;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *qptr;
    // itemsize: bytes in one quantum set
    int quantum = dev->quantum, qset = dev->qset, itemsize = quantum*qset;
    int item, s_pos, q_pos, rest;
    ssize_t ret = 0;
    // interruptible sleep
    if (down_interruptible(&dev->sem))
        // if interrupted: like a
        return -ERESTARTSYS;
    if (*f_pos >= dev->size) // EOF
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;
    /* Calculate positions */
    item = (long)*f_pos / itemsize; /* which quantum set? */
    rest = (long)*f_pos % itemsize; /* offset within the quantum set */
    s_pos = rest / quantum;  /* index of quantum in the set */
    q_pos = rest % quantum; /* offset within that quantum */
    /* Find the quantum set item position ahead */
    qptr =  scull_find_item(dev, item);
    if (qptr == NULL || !qptr->data || !qptr->data[s_pos])
    {
        /* No data to read */
        goto out;
    }
    /* limit read to this quantum's end */
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    /* Copy data to user space */
    if (copy_to_user(buf, qptr->data[s_pos] + q_pos, count))
    {
        ret = -EFAULT;
        goto out;
    }
    *f_pos = *f_pos + count;
    ret = count;

out:
    up(&dev->sem);
    return ret;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *qptr;
    // itemsize: bytes in one quantum set
    int quantum = dev->quantum, qset = dev->qset, itemsize = quantum*qset;
    int item, s_pos, q_pos, rest;
    ssize_t ret = -ENOMEM;
    // interruptible sleep
    if (down_interruptible(&dev->sem))
        // if interrupted: like a
            return -ERESTARTSYS;
    /* Calculate positions */
    item = (long)*f_pos / itemsize; /* which quantum set? */
    rest = (long)*f_pos % itemsize; /* offset within the quantum set */
    s_pos = rest / quantum;  /* index of quantum in the set */
    q_pos = rest % quantum; /* offset within that quantum */
    /* Find the quantum set item position ahead */
    qptr =  scull_find_item(dev, item);
    if (qptr == NULL)
    {
        /* No data to read */
        goto out;
    }
    if (!qptr->data)
    {
        /* Allocate a qset array*/
        qptr->data = kmalloc(qset* sizeof(char *), GFP_KERNEL);
        if (!qptr->data) goto out;
        memset(qptr->data, 0, qset* sizeof(char *));
    }
    /* allocate the specific quantum at [s_pos] if not present */
    if (!qptr->data[s_pos])
    {
        qptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!qptr->data[s_pos]) goto out;
    }

    /* limit read to this quantum's end */
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    /* Copy data to user space */
    if (copy_from_user(qptr->data[s_pos] + q_pos, buf, count))
    {
        ret = -EFAULT;
        goto out;
    }
    *f_pos = *f_pos + count;
    ret = count;
    /* update the device size */
    if (dev->size < *f_pos)
    {
        dev->size = *f_pos;

    }
    out:
        up(&dev->sem);
        return ret;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;
    switch (whence)
    {
        case SEEK_SET:
            newpos = off;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + off;
            break;
        case SEEK_END:
            newpos = dev->size;
            break;
        default: return -EINVAL;
    }
    if (newpos < 0)return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}
long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

}

struct file_operations scull_fops ={
    .owner = THIS_MODULE,
    .open = scull_open,
    .release = scull_release,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
};