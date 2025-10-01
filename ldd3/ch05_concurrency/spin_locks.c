// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Spinlock demo: IRQ + tasklet + procfs");
MODULE_LICENSE("GPL");

static int irq = -1;              /* if < 0, use hrtimer emulation */
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "IRQ number to hook (negative = use hrtimer)");

static unsigned int period_ms = 200; /* hrtimer period when emulating */
module_param(period_ms, uint, 0644);
MODULE_PARM_DESC(period_ms, "Emulated interrupt period (ms)");

/* Shared state protected by spinlock */
struct spin_demo {
	spinlock_t lock;
	u64 irq_count;     /* increments in top half */
	u64 bh_count;      /* increments in tasklet (bottom half) */

	/* Bottom half */
	struct tasklet_struct tasklet;

	/* Optional emulation */
	struct hrtimer timer;
	bool using_timer;

	/* IRQ handle */
	int irq;
	bool irq_registered;
} demo;

static void demo_tasklet_fn(struct tasklet_struct *t)
{
	struct spin_demo *d = container_of(t, struct spin_demo, tasklet);

	/* Softirq context: cannot sleep; IRQs may be enabled. */
	spin_lock(&d->lock);
	d->bh_count++;
	spin_unlock(&d->lock);
}

/* Real interrupt handler (top half) */
static irqreturn_t demo_irq_handler(int irq, void *dev_id)
{
	struct spin_demo *d = dev_id;

	/* Hard-IRQ context, local IRQs are off on this CPU already. */
	spin_lock(&d->lock);
	d->irq_count++;
	spin_unlock(&d->lock);

	/* Defer any heavier work to bottom half */
	tasklet_schedule(&d->tasklet);
	return IRQ_HANDLED;
}

/* Emulation via hrtimer: fires periodically to exercise the same paths */
static enum hrtimer_restart demo_hrtimer_fn(struct hrtimer *t)
{
	/* Call the same logic as an IRQ would */
	demo_irq_handler(-1, &demo);

	hrtimer_forward_now(&demo.timer, ms_to_ktime(period_ms));
	return HRTIMER_RESTART;
}

/* ---------- procfs: /proc/spin_demo shows counters ---------- */
static int demo_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	u64 irqc, bhc;

	/* Process context: must protect against hard IRQ -> use irqsave */
	spin_lock_irqsave(&demo.lock, flags);
	irqc = demo.irq_count;
	bhc  = demo.bh_count;
	spin_unlock_irqrestore(&demo.lock, flags);

	seq_printf(m,
		   "Spinlock demo counters\n"
		   "  irq_count: %llu\n"
		   "  bh_count : %llu\n"
		   "  mode     : %s\n",
		   irqc, bhc, demo.using_timer ? "hrtimer (emulated)" : "real IRQ");
	return 0;
}
static int demo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, demo_proc_show, NULL);
}
static const struct proc_ops demo_proc_fops = {
	.proc_open    = demo_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *proc_ent;

static int __init spin_demo_init(void)
{
	int ret = 0;

	spin_lock_init(&demo.lock);
	tasklet_setup(&demo.tasklet, demo_tasklet_fn);

	if (irq >= 0) {
		/* Safer to share unless you *own* the line */
		ret = request_irq(irq, demo_irq_handler, IRQF_SHARED,
				  "spin_demo", &demo);
		if (ret) {
			pr_warn("spin_demo: request_irq(%d) failed (%d), falling back to timer\n",
				irq, ret);
		} else {
			demo.irq = irq;
			demo.irq_registered = true;
			demo.using_timer = false;
		}
	}
	if (!demo.irq_registered) {
		/* Emulation path with periodic hrtimer */
		hrtimer_setup(&demo.timer, demo_hrtimer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
		demo.using_timer = true;
		hrtimer_start(&demo.timer, ms_to_ktime(period_ms), HRTIMER_MODE_REL_PINNED);
	}

	proc_ent = proc_create("spin_demo", 0444, NULL, &demo_proc_fops);
	if (!proc_ent) {
		ret = -ENOMEM;
		goto err_out;
	}

	pr_info("spin_demo: loaded (%s)\n", demo.using_timer ? "timer emulation" : "IRQ mode");
	return 0;

err_out:
	if (demo.using_timer)
		hrtimer_cancel(&demo.timer);
	if (demo.irq_registered)
		free_irq(demo.irq, &demo);
	tasklet_kill(&demo.tasklet);
	return ret;
}

static void __exit spin_demo_exit(void)
{
	if (proc_ent)
		proc_remove(proc_ent);

	if (demo.using_timer)
		hrtimer_cancel(&demo.timer);

	if (demo.irq_registered)
		free_irq(demo.irq, &demo);

	tasklet_kill(&demo.tasklet);
	pr_info("spin_demo: unloaded\n");
}

module_init(spin_demo_init);
module_exit(spin_demo_exit);
