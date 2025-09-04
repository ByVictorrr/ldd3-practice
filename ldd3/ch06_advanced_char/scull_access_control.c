#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/container_of.h>
#include <linux/poll.h>
#include "scull.h"
#include "scull_ioctl.h"
#include "scull_pipe.h"

// scullsinge - a device that only allows one open at a time - any 2nd open returns an error
// sculluid - singel user at a time - allows the same user to open multiple times, but if a diff user tries while one user has it open, that open will refuse
// scullwuid - similar to sculuid but instead of refusing a second user, it will wait until the device is free (blocking open)
// scullpriv - device that clones itself on open , giving each opener a separe data area (keyed by something like a process id or controlling tty
atomic_t scull_single_flag;
int scull_single_open(struct inode *inode, struct file *filp){
    if (atomic_dec_and_test(&scull_single_flag))
    {
        // if after decrement its not zero, someone else had it open
        atomic_inc(&scull_single_flag);
        return -EBUSY;
    }

}


struct file_operations scull_single_fops ={
    .owner = THIS_MODULE,
    .open = scull_p_open,
    .unlocked_ioctl = scull_ioctl,
    .llseek = no_llseek,
};


spinlock_t scull_uid_lock;


int scull_uid_open(struct inode *inode, struct file *filp)
{
    spin_lock(&scull_uid_lock);
    if (scull_u)


}
struct file_operations scull_uid_fops ={
    .owner = THIS_MODULE,
    .open = scull_p_open,
    .unlocked_ioctl = scull_ioctl,
    .llseek = no_llseek,
};
