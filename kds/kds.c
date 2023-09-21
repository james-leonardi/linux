#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init kds_init(void)
{
	printk(KERN_INFO "[KDS] Init\n");
	return 0;
}

static void __exit kds_exit(void)
{
	printk(KERN_INFO "[KDS] Exit\n");
}

module_init(kds_init);
module_exit(kds_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("Kernel Data Structures");
