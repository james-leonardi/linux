#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Leonardi <james.leonardi@stonybrook.edu>");
MODULE_DESCRIPTION("File System");

#define S2FS_MAGIC 0xE3256B
#define S2FS_PAGESHIFT 12
#define S2FS_PAGESIZE (1UL << S2FS_PAGESHIFT)

static struct super_operations s2fs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode
};

static int s2fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *root_dentry;

	/* Set up super_block struct. */
	sb->s_blocksize		= S2FS_PAGESIZE;
	sb->s_blocksize_bits	= S2FS_PAGESHIFT;
	sb->s_magic		= S2FS_MAGIC;
	sb->s_op		= &s2fs_s_ops;

	root = new_inode(sb);
	if (!root)
		return -ENOMEM;

	root->i_ino = 1;
	root->i_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	root->i_sb = sb;
	root->i_op = &simple_dir_inode_operations;
	root->i_fop = &simple_dir_operations;

	root_dentry = d_make_root(root);
	if (!root_dentry) {
		iput(root);
		return -ENOMEM;
	}

	sb->s_root = root_dentry;

	return 0;
}

static struct dentry *s2fs_get_super(struct file_system_type *fst,
		int flags, const char *devname, void *data)
{
	printk(KERN_INFO "s2fs_get_super\n");
	return mount_nodev(fst, flags, data, s2fs_fill_super);
}

static struct file_system_type s2fs_type = {
	.owner		= THIS_MODULE,
	.name		= "s2fs",
	.mount		= s2fs_get_super,
	.kill_sb	= kill_anon_super
};
	
/* Entry/exit functions */
static int __init s2fs_init(void)
{
	return register_filesystem(&s2fs_type);
}

static void __exit s2fs_exit(void)
{
	unregister_filesystem(&s2fs_type);
}

module_init(s2fs_init);
module_exit(s2fs_exit);
