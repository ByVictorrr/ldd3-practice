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
# define SCULL_QUANTUM 4096

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
#endif

