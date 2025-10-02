#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/container_of.h>
#include <linux/slab.h>
#include "scull.h"


int scull_major = SCULL_MAJOR;
int scull_minor = SCULL_MINOR;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_qset = SCULL_QSET;
int scull_quantum = SCULL_QUANTUM;
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_DESCRIPTION("Scullc");
MODULE_VERSION("0.2");
module_param(scull_major, int, 0444);
MODULE_PARM_DESC(scull_major, "Major number");

module_param(scull_minor, int, 0444);
MODULE_PARM_DESC(scull_minor, "Minor number");

module_param(scull_nr_devs, int, 0444);
MODULE_PARM_DESC(scull_nr_devs, "Number of SCULL Devices");

module_param(scull_qset, int, 0444);
MODULE_PARM_DESC(scull_qset, "How large should the qset be?");

module_param(scull_quantum, int, 0444);
MODULE_PARM_DESC(scull_quantum, "How large should the quantum be?");

static struct kmem_cache *scullc_cache;

static void * scullc_alloc_quantum()
{
    void *p = kmem_cache_alloc(scullc_cache, GFP_ATOMIC);
    if (p) memset(p, 0, scull_quantum);
    return p;
}
static void scullc_free_quantum(void *p)
{
    if (p) kmem_cache_free(scullc_cache, p);

}


int scull_dev_reset(struct scull_dev *dev)
{
    struct scull_qset *next, *curr;
    int i;
    /* Loop around all qsets */
    for (curr=dev->data; curr; curr=next)
    {
        if (curr->data){
            for (i=0; i < dev->qset; i++)
            {
                /* Free the ith Quantum */
                if (curr->data[i])
                {
                    scullc_free_quantum(curr->data[i]);
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
    {
        if (down_interruptible(&device->sem))
            return -ERESTARTSYS;
        scull_dev_reset(device);
        up(&device->sem);
    }
    return 0;
}
int scull_release(struct inode *inode,  struct file *filp){return 0;}

struct scull_qset *scull_find_item(struct scull_dev *dev, int item)
{
    int n = item;
    struct scull_qset *curr = dev->data;
    /* When the first node in ll is NULL */
    if (!curr)
    {
        curr = kzalloc(sizeof(struct scull_qset), GFP_KERNEL );

        if (!curr) return NULL;
        dev->data = curr;
    }

    while (--n > 0)
    {
        if (!curr->next)
        {
            curr->next = kzalloc(sizeof(struct scull_qset), GFP_KERNEL );
            if (!curr->next) return NULL;
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
        qptr->data = kcalloc(qset, sizeof(char *), GFP_KERNEL);
        if (!qptr->data) goto out;
    }
    /* allocate the specific quantum at [s_pos] if not present */
    if (!qptr->data[s_pos])
    {
        qptr->data[s_pos] = scullc_alloc_quantum();
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
    int q;
    long retval = -EINVAL;
    if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC || _IOC_NR(cmd) > SCULL_IOC_MAXNR)
        return -ENOTTY;
    if ((_IOC_DIR(cmd) & (_IOC_WRITE | _IOC_READ)) && !access_ok((void __user *)arg, _IOC_SIZE(cmd)))
        return -EFAULT;

    switch (cmd)
    {
    case SCULL_IOCRESET:
        scull_qset = SCULL_QSET;
        scull_quantum = SCULL_QUANTUM;
        break;
    /* "Set" value via pointer */
    case SCULL_IOCSQUANTUM:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        retval = __get_user(q, (int __user *)arg);
        scull_quantum = q;
        break;
    case SCULL_IOCSQSET:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        retval = __get_user(q, (int __user *)arg);
        scull_qset = q;
        break;
    /* "Tell" value via arg value */
    case SCULL_IOCTQUANTUM:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        scull_quantum = arg;
        break;
    case SCULL_IOCTQSET:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        scull_qset = arg;
        break;
    /* "Get" value via pointer */
    case SCULL_IOCGQUANTUM:

        retval = __put_user(scull_quantum, (int __user *)arg);
        break;
    case SCULL_IOCGQSET:
        retval = __put_user(scull_qset, (int __user *)arg);
        break;
    /* "Query" value via return value */
    case SCULL_IOCQQUANTUM:

        retval = scull_quantum;
        break;
    case SCULL_IOCQQSET:
        retval = scull_qset;
        break;
    /* "eXchange" value - atomic get and set */
    case SCULL_IOCXQUANTUM:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        q = scull_quantum;
        retval = __get_user(scull_quantum, (int __user *)arg);
        if (retval == 0)
            retval = __put_user(q, (int __user *)arg);
        break;
    case SCULL_IOCXQSET:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        q = scull_qset;
        retval = __get_user(scull_qset, (int __user *)arg);
        if (retval == 0)
            retval = __put_user(q, (int __user *)arg);
        break;
    /* "sHift" - toggling behavor*/
    case SCULL_IOCHQUANTUM:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        q = scull_quantum;
        scull_quantum = arg;
        retval = q;
        break;
    case SCULL_IOCHQSET:
        if (!capable(CAP_SYS_ADMIN))
        {
            retval = -EPERM;
            break;
        }
        q = scull_quantum;
        scull_quantum = arg;
        retval = q;
        break;
    default:
    }
    return retval;
}



const struct file_operations scull_fops ={
    .owner = THIS_MODULE,
    .open = scull_open,
    .release = scull_release,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .llseek = scull_llseek,
};


struct class * cls;
struct scull_dev *scull_devices;
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	dev_t devno = MKDEV(scull_major, scull_minor + index);
	// associate the cdev with file operations
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_NOTICE "scullc: cdev_add failed\n");
}

static void _scull_cleanup_module(void)
{
	dev_t devno = MKDEV(scull_major, scull_minor);
	int i;
	/* Get rid of our char dev enteries */

	if (scull_devices)
	{
		for (i=0; i < scull_nr_devs; i++)
		{
			scull_dev_reset(&scull_devices[i]);
			cdev_del(&scull_devices[i].cdev);
			device_destroy(cls, MKDEV(MAJOR(devno), scull_minor + i));
		}
		kfree(scull_devices);
	}
	unregister_chrdev_region(devno, scull_nr_devs);
	class_destroy(cls);
    if (scullc_cache)
        kmem_cache_destroy(scullc_cache);
}
static int __init scull_init(void) {
	dev_t dev;
	int result, i;

	if (scull_major)
	{
		/*
		* With given (major, start_minor) register scull_nr_devs with name "scull" in /proc/devices
		*/
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scullc");
	}else
	{
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scullc");
		scull_major = MAJOR(dev);
	}
	if (result < 0)
	{
		printk(KERN_WARNING "scullc: cant get major %d\n", scull_major);
		return result;
	}
	/* GFP_KERNEL */
	scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices)
	{
		result = -ENOMEM;
		goto fail;
	}
	struct scull_dev *device;

	// create class at /sys/class/scull
	cls = class_create("scullc");
	for (i=0; i<scull_nr_devs; i++)
	{
		device = &scull_devices[i];
		device->quantum = scull_quantum;
		device->qset    = scull_qset;
		device->size    = 0;
		device->data    = NULL;
		sema_init(&device->sem, 1);
		scull_setup_cdev(device, i);
		device_create(cls, NULL, MKDEV(scull_major, scull_minor + i), NULL, "scullc%d", i);
	}
    scullc_cache = kmem_cache_create("scullc", scull_quantum, 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!scullc_cache)
    {
        result = -ENOMEM;
        goto fail;
    }

	return 0;

	fail:
	_scull_cleanup_module();
	return result;
}


static void __exit scull_exit(void) {
	_scull_cleanup_module();
}

module_init(scull_init)
module_exit(scull_exit)