//
// Created by victord on 8/31/25.
//

#ifndef LDD3_PRACTICE_SCULL_PIPE_H
#define LDD3_PRACTICE_SCULL_PIPE_H
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>

struct scull_pipe{
    wait_queue_head_t inq, outq; // Wait queues for readers and writers
    char *start, *end; // star, end of circular buffer
    int buffersize; // size of the buffer
    char *rp, *wp; // read, write pointers
    int nreaders, nwriters; // number of open readers, writers
    struct fasync_struct *fasync_queue; // async notifier list (for SIGIO)
    struct semaphore sem; // mutual exclusion semaphore
    struct cdev cdev; // char device structure
};


int scull_pipe_init(dev_t fist_dev);
void scull_pipe_exit(void);
#endif //LDD3_PRACTICE_SCULL_PIPE_H