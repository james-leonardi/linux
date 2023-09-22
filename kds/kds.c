#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <linux/list.h>
#include <linux/rbtree.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("Kernel Data Structures");

static char *int_str = "";
static int *ints = NULL;
static size_t ints_count = 0;
static size_t ints_capacity = 0;

module_param(int_str, charp, 0);
MODULE_PARM_DESC(int_str, "List of space-separated integers to be parsed by the module");

/* ========== LINKED LIST ========== */

struct ll_node {
	struct list_head links;
	int value;
};

static struct list_head *ll_create(int *list, size_t list_size) {
	size_t count = 0;

	struct list_head *kds_ll = kmalloc(sizeof(struct list_head), GFP_KERNEL);
	INIT_LIST_HEAD(kds_ll);

	while (count < list_size) {
		struct ll_node *new = kmalloc(sizeof(struct ll_node), GFP_KERNEL);
		INIT_LIST_HEAD(&(new->links));
		new->value = *(list + count);
		list_add_tail(&(new->links), kds_ll);
		++count;
	}

	return kds_ll;
}

static void ll_print(struct list_head *ll) {
	struct ll_node *cursor;
	printk(KERN_INFO "[KDS] Linked List:");
	list_for_each_entry(cursor, ll, links) {
		printk(KERN_CONT " %d", cursor->value);
	}
	printk(KERN_CONT "\n");
}

static void ll_free(struct list_head *ll) {
	struct ll_node *cursor, *temp;
	list_for_each_entry_safe(cursor, temp, ll, links) {
		list_del(&(cursor->links));
		kfree(cursor);
	}
	kfree(ll);
}

/* ================================= */


/* ========== RED-BLACK TREE ========== */

struct rb_entry {
	struct rb_node node;
	int value;
};

static inline void rb_insert(struct rb_root *root, struct rb_entry *new) {
	struct rb_node **link = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct rb_entry *cursor;

	while (*link) {
		parent = *link;
		cursor = rb_entry(parent, struct rb_entry, node);
		if (new->value < cursor->value) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
		}
	}

	rb_link_node(&(new->node), parent, link);
	rb_insert_color(&(new->node), root);
}

static struct rb_root *rb_create(int *list, size_t list_size) {
	size_t count = 0;

	struct rb_root *root = kmalloc(sizeof(struct rb_root), GFP_KERNEL);
	*root = RB_ROOT;

	while (count < list_size) {
		struct rb_entry *new = kmalloc(sizeof(struct rb_entry), GFP_KERNEL);
		new->value = *(list + count);
		rb_insert(root, new);
		++count;
	}

	return root;
}

static void rb_print(struct rb_root *root) {
	struct rb_node *cursor;
	printk(KERN_INFO "[KDS] Red-Black Tree:");
	for (cursor = rb_first(root); cursor; cursor = rb_next(cursor)) {
		printk(KERN_CONT " %d", rb_entry(cursor, struct rb_entry, node)->value);
	}
	printk(KERN_CONT "\n");
}

/* ==================================== */


/* ========== HASHTABLE ========== */



/* =============================== */


/* Prints the memory allocated by 'ints', marking memory
 * not considered part of the array as [UNUSED] */
static inline void printarr(void) {
	size_t c = 0;
	if (ints == NULL) {
		printk(KERN_DEBUG "[KDS] No elements in 'ints' list\n");
		return;
	}
	printk(KERN_DEBUG "[KDS] ints_count = %lu\n", ints_count);
	while (c < ints_capacity) {
		printk(KERN_DEBUG "  [%lu/%lu] %i (%c) %s\n",
				c + 1,
				ints_capacity,
				*(ints + c),
				(char) *(ints + c),
				c < ints_count ? "" : "[UNUSED]");
		++c;
	}
}

static inline void printints(void) {
	size_t count = 0;
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
	struct list_head *ll;
	struct rb_root *rb;
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

	ll = ll_create(ints, ints_count);
	ll_print(ll);
	ll_free(ll);

	rb = rb_create(ints, ints_count);
	rb_print(rb);

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
