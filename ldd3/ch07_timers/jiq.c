// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/softirq.h>
#include <linux/timer.h>

struct proc_dir_entry *proc_entry;
int delay = 5;
module_param(delay, int, 0644);
MODULE_PARM_DESC(delay, "How long to delay the module to show");
MODULE_AUTHOR("Victor Delaplaine");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Example of showing deferred options");

// limit number of bytes to print
#define LIMIT (PAGE_SIZE - 128 )
// wait queue to block main thread on
static DECLARE_WAIT_QUEUE_HEAD(jiq_wait);

/*
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
    struct work_struct jiq_work;
    struct delayed_work jiq_delayed_work;
    struct timer_list jiq_timer;
    struct tasklet_struct jiq_tasklet;
    struct seq_file *m;
    int len;
    unsigned long jiffies;
} jiq_data;

static int jiq_print(struct clientdata *d)
{
    /* do the printing; return non-zero if the task should be rescheduled */
    int len = d->len;
    struct seq_file *m = d->m;
    unsigned long j = jiffies;
    if (len > LIMIT)
    {
        wake_up_interruptible(&jiq_wait); // wake up main thread
        return 0; // return - dont schedule
    }
    if (len == 0)
    {
        seq_puts(m, "time delta preempt pid cpu command\n");
        len = (long)m->count;
    }else
    {
        len = 0;
    }
    seq_printf(m, "%9li  %4li     %3i %5i %3i %s\n",
        j, j - d->jiffies,
        preempt_count(), current->pid, smp_processor_id(),
        current->comm);
    len += m->count;
    d->len += len;
    d->jiffies = j;
    return 1;


}
/*---------- work item - workqueue---------------*/
static void jiq_readwq_fn(struct work_struct *w)
{
    struct clientdata * d = container_of(w, struct clientdata, jiq_work);
    if (!jiq_print(d))
        return ;
    schedule_work(&d->jiq_work);
}
/*
 * This function prints one line of data, after sleeping one second.
 * It can sleep in different ways, according to the data pointer
 */
static int jiq_readwq_show(struct seq_file *m, void *v)
{

    /* init data */
    DEFINE_WAIT(wait);
    jiq_data.len = 0;
    jiq_data.m = m;
    jiq_data.jiffies = jiffies;

    /* prepare to sleep & schedule work item in work queue */
    prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE); // add to wait queue
    schedule_work(&jiq_data.jiq_work); // schedule work queue item
    schedule(); // yield the processor; changed state -> sleep
    finish_wait(&jiq_wait, &wait); // remove wait queue item from queue

    return 0;
}

static int jiq_readwq_open(struct inode *inode, struct file *file)
{
    return single_open(file, jiq_readwq_show, pde_data(inode));
}

static const struct proc_ops jit_readwq_fops = {
    .proc_open = jiq_readwq_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release	= single_release,
};

/*---------- delayed work item - workqueue---------------*/
static void jiq_readwq_delayed_fn(struct work_struct *w)
{
    struct clientdata * d = container_of((struct delayed_work *)w, struct clientdata, jiq_delayed_work);
    if (!jiq_print(d))
        return ;
    schedule_delayed_work(&d->jiq_delayed_work, delay);
}
static int jiq_readwq_delayed_show(struct seq_file *m, void *v)
{

    /* init data */
    DEFINE_WAIT(wait);
    jiq_data.len = 0;
    jiq_data.m = m;
    jiq_data.jiffies = jiffies;

    /* prepare to sleep & schedule work item in work queue */
    prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE); // add to wait queue
    schedule_delayed_work(&jiq_data.jiq_delayed_work, delay); // schedule work queue item
    schedule(); // yield the processor; changed state -> sleep
    finish_wait(&jiq_wait, &wait); // remove wait queue item from queue

    return 0;
}

static int jiq_readwq_delayed_open(struct inode *inode, struct file *file)
{
    return single_open(file, jiq_readwq_delayed_show, pde_data(inode));
}

static const struct proc_ops jiq_readwq_delayed_fops = {
    .proc_open = jiq_readwq_delayed_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release	= single_release,
};
/*--------- timers ---------------*/
static void jiq_timeout(struct timer_list *t)
{
    struct clientdata *data = container_of(t, struct clientdata, jiq_timer);
    if (!jiq_print(data))
    {
        wake_up_interruptible(&jiq_wait);  /* awake the process */
        return;
    }
    add_timer(&jiq_data.jiq_timer);
}

static int jiq_read_timer_show(struct seq_file *m, void *v)
{

    jiq_data.len = 0;
    jiq_data.m = m;
    jiq_data.jiffies = jiffies;

    /* init timer */
    timer_setup(&jiq_data.jiq_timer, jiq_timeout, 0);
    jiq_data.jiq_timer.expires = jiffies + HZ;

    /* start timer */
    jiq_print(&jiq_data);
    add_timer(&jiq_data.jiq_timer);
    wait_event_interruptible(jiq_wait, jiq_data.len > LIMIT); // not sure about 0 condition
    /* stop kernel timer & make sure its not running anymore */
    timer_shutdown_sync(&jiq_data.jiq_timer);

    return 0;
}
static int jiq_read_timer_open(struct inode *inode, struct file *file)
{
    return single_open(file, jiq_read_timer_show, pde_data(inode));
}

static const struct proc_ops jiq_timer_fops = {
    .proc_open = jiq_read_timer_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release	= single_release,
};
/*-----------tasklet-----------------*/
static void jiq_print_tasklet(unsigned long ptr)
{
    struct clientdata *data = (struct clientdata *)ptr;
    if (jiq_print(data))
        tasklet_schedule(&data->jiq_tasklet);
}

static int jiq_read_tasklet_show(struct seq_file *m, void *v)
{

    jiq_data.len = 0;
    jiq_data.m = m;
    jiq_data.jiffies = jiffies;

    /* init tasklet */
    tasklet_schedule(&jiq_data.jiq_tasklet);
    wait_event_interruptible(jiq_wait, jiq_data.len > LIMIT); // not sure about 0 condition

    return 0;
}
static int jiq_read_tasklet_open(struct inode *inode, struct file *file)
{
    return single_open(file, jiq_read_tasklet_show, pde_data(inode));
}

static const struct proc_ops jiq_tasklet_fops = {
    .proc_open = jiq_read_tasklet_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release	= single_release,
};

static int __init jiq_init(void)
{
    pr_info("jiq: loading\n");
    INIT_WORK(&jiq_data.jiq_work, jiq_readwq_fn);
    INIT_DELAYED_WORK(&jiq_data.jiq_delayed_work, jiq_readwq_delayed_fn);
    tasklet_init(&jiq_data.jiq_tasklet, jiq_print_tasklet,
            (unsigned long)&jiq_data);

    proc_entry = proc_mkdir("jiq", NULL);
    if (!proc_entry)
        return -ENOMEM;
    if (!proc_create_data("jiqwq", 0666,  proc_entry, &jit_readwq_fops, NULL))
        return -ENOMEM;
    if (!proc_create_data("jiqwqdelayed", 0666,  proc_entry, &jiq_readwq_delayed_fops, NULL))
        return -ENOMEM;
    if (!proc_create_data("jiqtimer", 0666,  proc_entry, &jiq_timer_fops, NULL))
        return -ENOMEM;
    if (!proc_create_data("jiqtasklet", 0666,  proc_entry, &jiq_tasklet_fops, NULL))
        return -ENOMEM;


    return 0;
}


static void __exit jiq_exit(void)
{
    remove_proc_subtree("jiq", NULL);
    pr_info("jiq: unloading\n");
}

module_init(jiq_init);
module_exit(jiq_exit);
