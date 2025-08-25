#ifndef _FOPS_H_
#define _FOPS_H_

#include <linux/semaphore.h>
#include <linux/cdev.h>


#define SCULL_MAJOR 0

int scull_major = SCULL_MAJOR;
module_param(scull_major, int, 0444);
MODULE_PARM_DESC(scull_major, "Major number");
#define SCULL_MINOR 0

int scull_minor = SCULL_MINOR;
module_param(scull_minor, int, 0444);
MODULE_PARM_DESC(scull_minor, "Minor number");

#define SCULL_NR_DEVS 4
int scull_nr_devs = SCULL_NR_DEVS;
module_param(scull_nr_devs, int, 0444);
MODULE_PARM_DESC(scull_nr_devs, "Number of SCULL Devices");

#define SCULL_QSET 1000
int scull_qset = SCULL_QSET;
module_param(scull_qset, int, 0444);
MODULE_PARM_DESC(scull_qset, "How large should the qset be?");

# define SCULL_QUANTUM 4096
int scull_quantum = SCULL_QSET;
module_param(scull_quantum, int, 0444);
MODULE_PARM_DESC(scull_quantum, "How large should the quantum be?");




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


};
struct scull_dev *scull_devices;
int scull_open(struct inode *, struct file *);
ssize_t scull_read(struct file *, char __user *, size_t, loff_t *);
ssize_t scull_write(struct file *, const char __user *, size_t, loff_t *);
#endif

