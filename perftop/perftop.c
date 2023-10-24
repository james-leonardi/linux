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

/* Declare the internal pick_next_task_fair() function. */
// extern struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev, struct rq_flags *rf);

/* Declare global variables. */
static int pre_count, post_count;

/* Output of proc file. */
static int perftop_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Pre-Count: %i\tPost-Count: %i\n", pre_count, post_count);
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
	pre_count++;
	return 0;
}

static int ret_pick_next_fair(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	post_count++;
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
	.maxactive = 10,
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
