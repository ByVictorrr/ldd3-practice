// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oops");
MODULE_DESCRIPTION("Faulty module demo");

static int __init jit_init(void)
{
    pr_info("faulty: loading\n");

    /* BUG: Dereferencing NULL pointer on purpose */
    *(int *)0 = 1234;

    return 0;
}

static void __exit jit_exit(void)
{
    pr_info("faulty: unloading\n");
}

module_init(jit_init);
module_exit(jit_exit);
