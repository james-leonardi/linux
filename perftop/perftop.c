#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("CPU Profiler");

/* Output of proc file. */
static int perftop_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Hello World\n");
	return 0;
}

/* Callback for when proc file is opened. */
static int perftop_open(struct inode *inode, struct file *file)
{
	/* Print out all data at once. */
	return single_open(file, perftop_show, NULL);
}

static const struct proc_ops perftop_ops = 
{
	.proc_open = perftop_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init perftop_init(void)
{
	proc_create("perftop", 0, NULL, &perftop_ops);
	return 0;
}

static void __exit perftop_exit(void)
{
	remove_proc_entry("perftop", NULL);
}

module_init(perftop_init);
module_exit(perftop_exit);
