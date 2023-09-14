#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

SYSCALL_DEFINE2(s2_encrypt, char __user *, str, int, key)
{
	unsigned long strlen;
	char *kstr;
	char *i;

	/* Check if key is within valid 1-5 range */
	if (unlikely(key < 1 || key > 5)) {
		printk(KERN_ERR "[s2_encrypt] Received out-of-bounds key\n");
		return EINVAL;
	}

	/* Get string length */
	strlen = strnlen_user(str, PAGE_SIZE);
	if (unlikely(strlen == 0 || strlen > PAGE_SIZE)) {
		printk(KERN_ERR "[s2_encrypt] Unable to fetch length of given string\n");
		return EINVAL;
	}

	/* Verify user pointer */
	if (!access_ok(str, strlen)) {
		printk(KERN_ERR "[s2_encrypt] Memory access error\n");
		return EINVAL;
	}

	/* Basic verification succeeded - perform operation */

	/* Copy string to kernel space */
	kstr = kmalloc(strlen, GFP_KERNEL);
	if (unlikely(copy_from_user(kstr, str, strlen))) {
		printk(KERN_ERR "[s2_encrypt] Failed to copy string from userspace\n");
		kfree(kstr);
		return EINVAL;
	}

	/* Add 'key' to each char in string */
	for (i = kstr; *i; i++)
		*i += key;

	/* Copy string back to user space */
	if (unlikely(copy_to_user(str, kstr, strlen))) {
		printk(KERN_ERR "[s2_encrypt] Failed to copy string to userspace\n");
		kfree(kstr);
		return EINVAL;
	}

	/* Print out encrypted string to kernel log */
	printk(KERN_INFO "%s\n", kstr);
	kfree(kstr);
	return 0;
}
