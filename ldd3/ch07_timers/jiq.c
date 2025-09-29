// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oops");
MODULE_DESCRIPTION("Faulty module demo");
struct proc_dir_entry *proc_entry;
int delay = 5;
enum jit_files {
    JIT_BUSY,
    JIT_SCHED,
    JIT_QUEUE,
    JIT_SCHEDTO
};


module_param(delay, int, 0444);
MODULE_PARM_DESC(delay, "How long to delay the module to show");
/*
 * This function prints one line of data, after sleeping one second.
 * It can sleep in different ways, according to the data pointer
 */
static int jit_fn_show(struct seq_file *m, void *v)
{
    unsigned long j0, j1; /* jiffies */
    wait_queue_head_t wait;
    long data = (long)m->private;

    init_waitqueue_head(&wait);
    j0 = jiffies;
    j1 = j0 + delay;

    switch (data) {
        case JIT_BUSY:
            while (time_before(jiffies, j1))
                cpu_relax(); // busy-wait
            break;
        case JIT_SCHED:
            while (time_before(jiffies, j1))
                schedule(); // yield
            break;
        case JIT_QUEUE:
            wait_event_interruptible_timeout(wait, 0, delay); // no condition
            break;
        case JIT_SCHEDTO:
            set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(delay); // kernel timer does sets task to TASK_RUNNING after dealy
            break;
        default:
            return -EINVAL;

    }
    j1 = jiffies; /* actual value after we delayed */

    seq_printf(m, "%9li %9li\n", j0, j1);
    return 0;
}

static int jit_fn_open(struct inode *inode, struct file *file)
{
    return single_open(file, jit_fn_show, pde_data(inode));
}

static const struct proc_ops jit_fn_fops = {
    .proc_open = jit_fn_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release	= single_release,
};
/*
 * This file, on the other hand, returns the current time forever
 */
int jit_currentime_show(struct seq_file *m, void *v)
{
    unsigned long j1;
    u64 j2;

    /* get them four */
    j1 = jiffies;
    j2 = get_jiffies_64();
    struct timespec64 tv1;
    struct timespec64 tv2;
    ktime_get_real_ts64(&tv1); // higher precision
    ktime_get_coarse_real_ts64(&tv2); // much lower precision
    seq_printf(m, "0x%08lx 0x%016Lx %10i.%09i\n"
           "%40i.%09i\n",
           j1, j2,
           (int) tv1.tv_sec, (int) tv1.tv_nsec,
           (int) tv2.tv_sec, (int) tv2.tv_nsec);

    return 0;
}

static int jit_currentime_open(struct inode *inode, struct file *file){
    return single_open(file, jit_currentime_show, NULL);
}

static const struct proc_ops jit_currentime_fops = {
    .proc_open		= jit_currentime_open,
    .proc_read		= seq_read,
    .proc_lseek		= seq_lseek,
    .proc_release	= single_release,
};
static int __init jit_init(void)
{
    pr_info("faulty: loading\n");

    proc_entry = proc_mkdir("jit", NULL);
    if (!proc_entry)
        return -ENOMEM;
    if (!proc_create_data("currentime", 0666,  proc_entry, &jit_currentime_fops, NULL))
        return -ENOMEM;
    if (!proc_create_data("jitbusy", 0666, proc_entry, &jit_fn_fops, (void*)JIT_BUSY))
        return -ENOMEM;


    return 0;
}

static void __exit jit_exit(void)
{
    remove_proc_subtree("jit", NULL);
    pr_info("faulty: unloading\n");
}

module_init(jit_init);
module_exit(jit_exit);
