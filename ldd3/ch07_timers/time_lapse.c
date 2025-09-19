// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiffies");
MODULE_DESCRIPTION("Using Jiffies module demo");

static int __init jiffies_init(void)
{

    unsigned long j, n, stamp_1, stamp_half, stamp_n;
    n=10;
    j = jiffies; // read the current tick count
    stamp_1 = j + HZ; // 1 second in the future
    stamp_half = j + HZ/2; /// half a second in the future
    stamp_n = j + n * HZ/1000; // n miliseconds in the future

    if (time_after())

    pr_info("faulty: loading\n");

    return 0;
}

static void __exit jiffies_exit(void)
{
    pr_info("faulty: unloading\n");
}

module_init(jiffies_init);
module_exit(jiffies_exit);
