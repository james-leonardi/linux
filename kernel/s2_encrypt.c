#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

SYSCALL_DEFINE2(s2_encrypt, char __user *, str, int, key)
{
	/* Check if key is within valid 1-5 range */
	if (unlikely(key < 1 || key > 5))
		return EINVAL;

	/* Get string length */
	unsigned long strlen = strnlen_user(str, PAGE_SIZE);
	if (unlikely(strlen == 0 || strlen > PAGE_SIZE))
		return EINVAL;

	/* Verify user pointer */
	if (!access_ok(VERIFY_WRITE, str, strlen))
		return EINVAL;

	/* Basic verification succeeded - perform operation */

	/* Copy string to kernel space */
	char *kstr = kmalloc(strlen, GFP_KERNEL);
	if (unlikely(copy_from_user(kstr, str, strlen))) {
		printk(KERN_ERR "[s2_encrypt] Failed to copy string from userspace");
		kfree(kstr);
		return EINVAL;
	}

	/* Add 'key' to each char in string */
	for (char *i = kstr; kstr; kstr++) {
		*i += key;
	}

	/* Copy string back to user space */
	if (unlikely(copy_to_user(str, kstr, strlen))) {
		printk(KERN_ERR "[s2_encrypt] Failed to copy string to userspace");
		kfree(kstr);
		return EINVAL;
	}

	/* Print out encrypted string to kernel log */
	printk(KERN_INFO kstr);
	kfree(kstr);
	return 0;
}
