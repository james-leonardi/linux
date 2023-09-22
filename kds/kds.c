#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/hashtable.h>
#include <linux/radix-tree.h>

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

static void rb_free(struct rb_root *root) {
	struct rb_node *cursor;
	struct rb_entry *entry;
	while ((cursor = rb_first(root))) {
		entry = rb_entry(cursor, struct rb_entry, node);
		rb_erase(&(entry->node), root);
		kfree(entry);
	}
	kfree(root);
}

/* ==================================== */


/* ========== HASHTABLE ========== */

struct ht_entry {
	struct hlist_node list;
	int value;
};

DEFINE_HASHTABLE(ht, 10);
static void ht_create(int *list, size_t list_size) {
	size_t count = 0;

	while (count < list_size) {
		struct ht_entry *new = kmalloc(sizeof(struct ht_entry), GFP_KERNEL);
		new->value = *(list + count);
		hash_add(ht, &(new->list), new->value);
		++count;
	}
}

static void ht_print(void) {
	int bkt;
	struct ht_entry *cursor;
	
	printk(KERN_INFO "[KDS] Hash Table:");
	hash_for_each(ht, bkt, cursor, list) {
		printk(KERN_CONT " %d", cursor->value);
	}
	printk(KERN_CONT "\n");
}

static void ht_print_possible(void) {
	int current_key;
	struct ht_entry *cursor;

	printk(KERN_INFO "[KDS] Hash Table (by key): {");
	/* Loop over every possible key */
	for (current_key = 0; current_key < 1024; current_key++) {
		printk(KERN_CONT " ");
		hash_for_each_possible(ht, cursor, list, current_key) {
			printk(KERN_CONT "%d ", cursor->value);
		}
		printk(KERN_CONT "|");
	}
	printk(KERN_CONT "}\n");
}


static void ht_free(void) {
	struct ht_entry *cursor;
	struct hlist_node *temp;
	int bkt;
	hash_for_each_safe(ht, bkt, temp, cursor, list) {
		hash_del(&(cursor->list));
		kfree(cursor);
	}
}

/* =============================== */


/* ========== RADIX TREE ========== */

#define RT_IS_ODD 0

static struct radix_tree_root *rt_create(int *list, size_t list_size) {
	size_t count = 0;
	int *num;

	struct radix_tree_root *root = kmalloc(sizeof(struct radix_tree_root), GFP_KERNEL);
	INIT_RADIX_TREE(root, GFP_KERNEL);

	while (count < list_size) {
		num = kmalloc(sizeof(int), GFP_KERNEL);
		*num = *(list + count);
		radix_tree_insert(root, *num, num);
		++count;
	}

	return root;
}

static void rt_print(struct radix_tree_root *root) {
	void **slot;
	struct radix_tree_iter iter;
	int *element;

	printk(KERN_INFO "[KDS] Radix-Tree: [");
	radix_tree_for_each_slot(slot, root, &iter, 0) {
		element = (int *)*slot;
		if (!element) {
			printk(KERN_CONT "{%lu:%s}", iter.index, "ERR");
			continue;
		}
		printk(KERN_CONT " {%lu:%d}", iter.index, *element);
	}
	printk(KERN_CONT " ]\n");
}

static size_t rt_tag_odd(struct radix_tree_root *root) {
	size_t tagged;
	void **slot;
	struct radix_tree_iter iter;
	int *element;

	radix_tree_for_each_slot(slot, root, &iter, 0) {
		element = (int *)*slot;
		if (!element || (*element % 2 == 0))
			continue;
		radix_tree_tag_set(root, iter.index, RT_IS_ODD);
		++tagged;
	}

	return tagged;
}

#define RT_MAX_RESULTS 1000
static void rt_print_odd(struct radix_tree_root *root) {
	void **results;
	unsigned int results_count, count = 0;

	results = kmalloc(sizeof(int) * RT_MAX_RESULTS, GFP_KERNEL);
	results_count = radix_tree_gang_lookup_tag(root, results, 0, RT_MAX_RESULTS, RT_IS_ODD);

	printk(KERN_INFO "[KDS] Radix-Tree (odd elements):");
	while (count < results_count) {
		printk(KERN_CONT " %d", *(*((int**)(results) + count)));
		++count;
	}
	printk(KERN_CONT "\n");
}
#undef RT_MAX_RESULTS

static void rt_free(struct radix_tree_root *root) {
	void **slot;
	struct radix_tree_iter iter;

	radix_tree_for_each_slot(slot, root, &iter, 0) {
		/* printk(KERN_INFO "%d\n", **(int**)slot); */
		radix_tree_delete(root, iter.index);
	}

	kfree(root);
}

#undef RT_IS_ODD

/* ================================ */

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
		printk(KERN_DEBUG "  [%lu/%lu] %d (%c) %s\n",
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
	struct radix_tree_root *rt;

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
	rb_free(rb);

	ht_create(ints, ints_count);
	ht_print();
	ht_print_possible();
	ht_free();

	rt = rt_create(ints, ints_count);
	rt_print(rt);
	rt_tag_odd(rt);
	rt_print_odd(rt);
	rt_free(rt);

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
