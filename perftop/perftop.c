#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/rbtree.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("CPU Profiler");

/* Define data structure nodes. */
struct pid_starttsc {
	pid_t pid;
	unsigned long start_tsc;
	struct hlist_node node;
};
struct pid_totaltsc {
	pid_t pid;
	unsigned long total_tsc;
	struct rb_node node;
};

/* Declare global variables. */
static int pre_count, post_count, context_switch_count;
static DEFINE_SPINLOCK(pre_count_lock);
static DEFINE_SPINLOCK(post_count_lock);
static DECLARE_HASHTABLE(start_tsc, 10); /* Used to translate PID -> start TSC */
static struct rb_root total_tsc = RB_ROOT; /* Used to keep track of total TSC */

/** Declare functions. **/
/* Read 'rdtsc' and return its value. */
static unsigned long get_rdtsc(void)
{
	unsigned long lo, hi;
	asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	lo |= hi << 32;
	return lo;
}

/* Get the entry into the hashtable for the given pid. */
static struct pid_starttsc *get_pid_starttsc(pid_t pid)
{
	struct pid_starttsc *cursor;
	hash_for_each_possible(start_tsc, cursor, node, pid) {
		if (cursor->pid == pid)
			return cursor;
	}
	return NULL;
}

/* Set the start time for the given struct. */
static void set_pid_starttsc(pid_t pid, unsigned long tsc, struct pid_starttsc *ptr)
{
	if (!ptr) {
		ptr = kmalloc(sizeof(struct pid_starttsc), GFP_KERNEL);
		ptr->pid = pid;
		hash_add(start_tsc, &ptr->node, ptr->pid);
	}
	ptr->start_tsc = tsc;
}

/* Get the entry into the rbtree for the given pid. */
static struct pid_totaltsc *get_pid_totaltsc(pid_t pid)
{
	struct pid_totaltsc *cursor, *temp;
	rbtree_postorder_for_each_entry_safe(cursor, temp, &total_tsc, node) {
		if (cursor->pid == pid)
			return cursor;
	}
	return NULL;
}

/* Update the total running time for the given pid. */
static void add_pid_totaltsc(pid_t pid, unsigned long to_add, struct pid_totaltsc *ptr)
{
	struct rb_node **new, *parent;
	
	/* Remove old entry into rbtree. */
	if (ptr) {
		to_add += ptr->total_tsc;
		rb_erase(&ptr->node, &total_tsc);
		kfree(ptr);
	}

	/* Insert new node with value tsc. */
	ptr = kmalloc(sizeof(struct pid_totaltsc), GFP_KERNEL);
	ptr->pid = pid;
	ptr->total_tsc = to_add;

	new = &total_tsc.rb_node;
       	parent = NULL;
	while (*new) {
		struct pid_totaltsc *this = container_of(*new, struct pid_totaltsc, node);
		parent = *new;
		if (this->total_tsc < ptr->total_tsc)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&ptr->node, parent, new);
	rb_insert_color(&ptr->node, &total_tsc);
}

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
	unsigned long flags, current_tsc, elapsed_time;
	struct task_struct *prev_task, *next_task;
	struct pid_starttsc *prev_start, *next_start;
	struct pid_totaltsc *prev_total;

	/* Increment post_count */
	spin_lock_irqsave(&post_count_lock, flags);
	post_count++;

	/* Retrieve task structs */
	next_task = (struct task_struct*)regs->ax;
	prev_task = (struct task_struct*)ri->data;
	if (!prev_task || !next_task)
		goto null_task;

	/* Compare PIDs */
	if (prev_task->pid == next_task->pid)
		goto no_context_switch;
	
	/* At this point, there has been a context switch.
	 * Increment the counter and prepare to calculate tsc. */
	context_switch_count++;
	current_tsc = get_rdtsc(); /* Timestamp this moment for comparisons. */

	/* Retrieve the prev_task start and total time. */
	prev_start = get_pid_starttsc(prev_task->pid);
	prev_total = get_pid_totaltsc(prev_task->pid);
	elapsed_time = prev_start ? current_tsc - prev_start->start_tsc : 0;
	add_pid_totaltsc(prev_task->pid, elapsed_time, prev_total);

	/* Retrieve and update the next_task start time. */
	next_start = get_pid_starttsc(next_task->pid);
	set_pid_starttsc(next_task->pid, current_tsc, next_start);

null_task:
no_context_switch:
	spin_unlock_irqrestore(&post_count_lock, flags);
	return 0;
}

/* Declare structs */
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

/* Entry/exit functions */
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
