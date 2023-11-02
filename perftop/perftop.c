#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("CPU Profiler");

/* Declare global variables. */
static int pre_count, post_count, context_switch_count;
static DEFINE_SPINLOCK(pre_count_lock);
static DEFINE_SPINLOCK(post_count_lock);

/* Output of proc file. */
static int perftop_show(struct seq_file *m, void *v)
{
	unsigned long pre_flags, post_flags;
	spin_lock_irqsave(&pre_count_lock, pre_flags);
	spin_lock_irqsave(&post_count_lock, post_flags);
	seq_printf(m, "Pre-Count: %i\tPost-Count: %i\tContext Switches:%i\n",
			pre_count, post_count, context_switch_count);
	spin_unlock_irqrestore(&post_count_lock, post_flags);
	spin_unlock_irqrestore(&pre_count_lock, pre_flags);
	return 0;
}

/* Callback for when proc file is opened. */
static int perftop_open(struct inode *inode, struct file *file)
{
	/* Print out all data at once. */
	return single_open(file, perftop_show, NULL);
}

static int entry_pick_next_fair(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long flags;
	struct task_struct *prev_task;

	/* Increment pre_count */
	spin_lock_irqsave(&pre_count_lock, flags);
	pre_count++;
	spin_unlock_irqrestore(&pre_count_lock, flags);

	/* Retrieve previous task PID */
	prev_task = (struct task_struct*)regs->si;

	/* Set kretprobe data */
	memcpy(ri->data, prev_task, sizeof(struct task_struct));
	return 0;
}

static int ret_pick_next_fair(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long flags;
	struct task_struct *prev_task, *next_task;

	/* Increment post_count */
	spin_lock_irqsave(&post_count_lock, flags);
	post_count++;

	/* Retrieve task structs */
	next_task = (struct task_struct*)regs->ax;
	prev_task = (struct task_struct*)ri->data;
	if (!prev_task || !next_task)
		goto null_task;

	/* Compare PIDs */
	if (prev_task->pid != next_task->pid)
		context_switch_count++;

null_task:
	spin_unlock_irqrestore(&post_count_lock, flags);
	return 0;
}

static const struct proc_ops perftop_ops = {
	.proc_open = perftop_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct kretprobe cfs_probe =
{
	.entry_handler = entry_pick_next_fair,
	.handler = ret_pick_next_fair,
	.data_size = sizeof(struct task_struct),
};

static int __init perftop_init(void)
{
	/* Create proc file. */
	proc_create("perftop", 0, NULL, &perftop_ops);

	/* Set up kretprobe on pick_next_task_fair(). */
	cfs_probe.kp.symbol_name = "pick_next_task_fair";
	if (register_kretprobe(&cfs_probe) < 0) {
		pr_err("Failed to register kretprobe\n");
		return -1;
	}
	pr_info("Registered probe at %p\n", cfs_probe.kp.addr);
	
	return 0;
}

static void __exit perftop_exit(void)
{
	/* Unregister kretprobe. Missed probes should be 0. */
	unregister_kretprobe(&cfs_probe);
	pr_info("Missed probes: %d\n", cfs_probe.nmissed);

	/* Remove proc file. */
	remove_proc_entry("perftop", NULL);
}

module_init(perftop_init);
module_exit(perftop_exit);
