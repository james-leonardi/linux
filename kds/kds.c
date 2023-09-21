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
static unsigned int count = 0;
static int *ints = NULL;
static int capacity = 0;

module_param(int_str, charp, 0);
MODULE_PARM_DESC(int_str, "List of space-separated integers to be parsed by the module");

static inline void printarr(void) {
	int c = 0;
	while (c < capacity) {
		printk(KERN_DEBUG "  [%i/%i] %i (%c)", c + 1, capacity, *(ints + c), *(ints + c));
		c++;
	}
}

#define INTS_PG_SZ 4
static int __init kds_init(void)
{
	char *token;
	long conv;
	printk(KERN_DEBUG "[KDS] Init\n");
	printk(KERN_DEBUG "[KDS] Received parameter: %s\n", int_str);

	/* Loop through string and extract each int */
	while ((token = strsep(&int_str, " ")) != NULL) {
		printk(KERN_DEBUG "[KDS] Next token: %s\n", token);
		
		/* Verify we have enough storage to store number + null term */
		if (count + 1 >= capacity) {
			/* We've run out of space in the int array.
			 * Allocate more storage */
			ints = krealloc(ints, (capacity + INTS_PG_SZ) * sizeof(int), GFP_KERNEL);
			memset(ints + capacity, 0, INTS_PG_SZ * sizeof(int));
			capacity += INTS_PG_SZ;
		}

		/* Verify legitimate number encountered */
		if (kstrtol(token, 10, &conv)) {
			printk(KERN_DEBUG "[KDS] Invalid token encountered, skipping");
			continue;
		}
		*(ints + count) = (int)conv;
		++count;
	}
	printk(KERN_DEBUG "[KDS] Reached end of int_str\n");
	printarr();
	return 0;
}
#undef INTS_PG_SZ

static void __exit kds_exit(void)
{
	printk(KERN_DEBUG "[KDS] Exit\n");
}

module_init(kds_init);
module_exit(kds_exit);
