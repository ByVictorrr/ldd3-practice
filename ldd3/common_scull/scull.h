#ifndef _SCULL_H_
#define _SCULL_H_
#pragma once
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#define SCULL_MAJOR 0
#define SCULL_MINOR 0
#define SCULL_NR_DEVS 4
#define SCULL_QSET 1000
#define SCULL_QUANTUM 4096

extern int scull_major;
extern int scull_minor;
extern int scull_nr_devs;
extern int scull_qset;
extern int scull_quantum;




struct scull_qset{
    void **data;
    struct scull_qset *next;


};
struct scull_dev{
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned long size;
    unsigned int access_key;
    struct semaphore sem;
    struct cdev cdev;
    /* Added for ch15 - scullv*/
    int vmas;


};

extern const struct file_operations scull_fops;  // used by main.c

int scull_dev_reset(struct scull_dev *);
int scull_open(struct inode *, struct file *);
int scull_release(struct inode *, struct file *);
ssize_t scull_read(struct file *, char __user *, size_t, loff_t *);
ssize_t scull_write(struct file *, const char __user *, size_t, loff_t *);
loff_t scull_llseek(struct file *, loff_t, int );
long scull_ioctl(struct file *, unsigned int, unsigned long );
struct scull_qset *scull_find_item(struct scull_dev *dev, int item);

#define SCULL_IOC_MAGIC 'k' /* MAGIC Number representing a scull ioctl cmd */
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0) /* reset to defaults */

#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int) /* "Set" quantum via pointer */
#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 2, int) /* "Set" qset via pointer */

#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)  /* "Tell" quantum via arg value */
#define SCULL_IOCTQSET _IO(SCULL_IOC_MAGIC, 4)  /* "Tell" qset via arg value */

#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)  /* "Get" Quantum via pointer */
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 6, int)  /* "Get" QSet via pointer */

#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7) /* "Query" quantum via return value */
#define SCULL_IOCQQSET _IO(SCULL_IOC_MAGIC, 8) /* "Query" qset via return value */

#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int) /* "eXchange" quantum - atomic get and set */
#define SCULL_IOCXQSET _IOWR(SCULL_IOC_MAGIC, 10, int) /* "eXchange" qset - atomic get and set */
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11) /* "sHift" - toggling behavior */
#define SCULL_IOCHQSET _IO(SCULL_IOC_MAGIC, 12) /* "sHift" - toggling behavior */
#define SCULL_IOC_MAXNR 14
#endif

