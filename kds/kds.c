#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("Kernel Data Structures");

static char *int_str = "";
static int *ints = NULL;
static unsigned int ints_count = 0;
static unsigned int ints_capacity = 0;

module_param(int_str, charp, 0);
MODULE_PARM_DESC(int_str, "List of space-separated integers to be parsed by the module");

/* Prints the memory allocated by 'ints', marking memory
 * not considered part of the array as [UNUSED] */
static inline void printarr(void) {
	int c = 0;
	if (ints == NULL) {
		printk(KERN_DEBUG "[KDS] No elements in 'ints' list\n");
		return;
	}
	printk(KERN_DEBUG "[KDS] ints_count = %i\n", ints_count);
	while (c < ints_capacity) {
		printk(KERN_DEBUG "  [%i/%i] %i (%c) %s\n",
				c + 1,
				ints_capacity,
				*(ints + c),
				(char) *(ints + c),
				c < ints_count ? "" : "[UNUSED]");
		++c;
	}
}

static inline void printints(void) {
	int count = 0;
	printk(KERN_INFO "[KDS] Integer list:");
	while (count < ints_count) {
		printk(KERN_CONT " %d", *(ints + count));
		++count;
	}
	printk(KERN_CONT "\n");
}

#define INTS_PG_SZ 32
static int __init kds_init(void)
{
	char *token;
	long num;
	printk(KERN_DEBUG "[KDS] Init\n");
	printk(KERN_DEBUG "[KDS] Received parameter: %s\n", int_str);

	/* Loop through string and extract each int */
	while ((token = strsep(&int_str, " ")) != NULL) {
		printk(KERN_DEBUG "[KDS] Next token: %s\n", token);
		
		/* Verify legitimate number encountered */
		if (kstrtol(token, 10, &num)) {
			printk(KERN_DEBUG "  This token is invalid, skipping\n");
			continue;
		}

		/* Verify we have enough storage to store new number */
		if (ints_count == ints_capacity) {
			/* We've run out of space in the int array. Allocate more storage. */
			ints_capacity += INTS_PG_SZ;
			ints = krealloc(ints, ints_capacity * sizeof(int), GFP_KERNEL);
		}

		/* Store new integer and increase the count */
		*(ints + ints_count) = (int)num;
		++ints_count;
	}
	printk(KERN_DEBUG "[KDS] Reached end of int_str\n");
	printarr();
	printints();
	return 0;
}
#undef INTS_PG_SZ

static void __exit kds_exit(void)
{
	if (ints) kfree(ints);
	printk(KERN_DEBUG "[KDS] Exit\n");
}

module_init(kds_init);
module_exit(kds_exit);
