/*
 * Copyright (c) 2016    Mukul Sharma
 * Copyright (c) 2016 Stony Brook University
 * Copyright (c) 2016 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "trfs.h"

#include <linux/module.h>



struct trfs_data {
      char *lower_path_name;
      void *trfs_option;
};

#define SAFE_PUTNAME(_name_)	        		\
do {		                      				    \
    if ((_name_) && !IS_ERR((_name_))) {  \
      putname((_name_));		              \
    }			                      		      \
} while(0)

#define SAFE_FILPCLOSE(_filp_)			      \
do {						                          \
    if ((_filp_) && !IS_ERR((_filp_))) { 	\
      filp_close((_filp_), NULL);	        \
    }					                            \
} while(0)

#define TFILE_LEN 6

#define MDBG          \
    {                 \
        UDBG;          \
        err = -EINVAL;\
        goto out;     \
    }
static int name_index(void *data) {
    char *option = (char*)data;
    if (!strncmp(option,"tfile=",TFILE_LEN)) {
      return TFILE_LEN;
    } else {
      return -EINVAL;
    }
}
  
struct file* create_trfile(void *raw_data)
{
  struct file *filep;
  int index;
  char *pathname = NULL; 
  struct filename *logfile = NULL;
  struct file *logfilp = NULL;
  mode_t  mode = S_IRUSR;

  if (raw_data == NULL) {
    printk(KERN_ERR"trfs: mount: missing tracefile path\n");
    filep = ERR_PTR(-EINVAL);
    goto out;
  }

  index = name_index(raw_data);
  if (index == TFILE_LEN ) {  
    pathname = (char*)raw_data+ TFILE_LEN;
  } else {
    UDBG;
    filep  = ERR_PTR(index);
    goto out;
  }

  logfile = getname_kernel(pathname);
  if (IS_ERR(logfile)) {
    UDBG;
    filep = (struct file*)logfile;
    goto out;
  }
  
  logfilp = filp_open(logfile->name, O_CREAT | O_RDWR | O_EXCL, mode);
  if (IS_ERR(logfilp)) {
    UDBG;
    filep = (struct file*)logfilp;
    goto out;
  }
  filep = logfilp;
  filep->f_pos = 0;
out:
  printk("options:%s err:%ld\n", pathname, IS_ERR(filep) ? PTR_ERR(filep): 0);
  SAFE_PUTNAME(logfile);
  return filep;
}


/*
 * There is no need to lock the trfs_super_info's rwsem as there is no
 * way anyone can have a reference to the superblock at this point in time.
 */
static int trfs_read_super(struct super_block *sb, void *raw_data, int silent)
{
	int err = 0;
	struct super_block *lower_sb;
	struct path lower_path;
  struct trfs_data *trdata = (struct trfs_data*)raw_data;
	char *dev_name = trdata->lower_path_name;
  void  *tr_option = trdata->trfs_option;
	struct inode *inode;
  struct file *tr_lower_filep;

  
	if (!dev_name) {
		printk(KERN_ERR
		       "trfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}

	/* parse lower path */
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		printk(KERN_ERR	"trfs: error accessing "
		       "lower directory '%s'\n", dev_name);
		goto out;
	} else {
      printk("dev name path:%s\n",dev_name);
  }

	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct trfs_sb_info), GFP_KERNEL);
	if (!TRFS_SB(sb)) {
		printk(KERN_CRIT "trfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}
  
  TRFS_PAGE(sb) = kmalloc(sizeof(struct tr_record) + PAGE_SIZE, GFP_KERNEL);
  if (!TRFS_PAGE(sb)) {
      printk(KERN_CRIT "trfs: read_super: out of memory\n");
      err = -ENOMEM;
      goto out_fsi_free;
  }

  
  tr_lower_filep = create_trfile(tr_option);
  
  if (IS_ERR(tr_lower_filep)) {
      err = PTR_ERR(tr_lower_filep);
      goto out_fsp_free;
  }
	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	trfs_set_lower_super(sb, lower_sb);

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_op = &trfs_sops;

	sb->s_export_op = &trfs_export_ops; /* adding NFS support */

	/* get a new inode and allocate our root dentry */
	inode = trfs_iget(sb, d_inode(lower_path.dentry));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_iput;
	}
	d_set_d_op(sb->s_root, &trfs_dops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* if get here: cannot have error */

	/* set the lower dentries for s_root */
	trfs_set_lower_path(sb->s_root, &lower_path);

	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_make_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);
	if (!silent)
		printk(KERN_INFO
		       "trfs: mounted on top of %s type %s\n",
		       dev_name, lower_sb->s_type->name);

  /* Private TRFS info */
  TRFS_SB(sb)->tr_lower_filep = tr_lower_filep;
  TRFS_SB(sb)->umask = -1;
  mutex_init(&(TRFS_SB(sb)->trfs_lock));

	goto out; /* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	dput(sb->s_root);
out_iput:
	iput(inode);
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
	kfree(TRFS_SB(sb));
	sb->s_fs_info = NULL;
out_fsp_free:
  kfree(TRFS_PAGE(sb));
out_fsi_free:
  kfree(TRFS_SB(sb));
out_free:
	path_put(&lower_path);

out:
	return err;
}


struct dentry *trfs_mount(struct file_system_type *fs_type, int flags,
			    const char *dev_name, void *raw_data)
{
  struct trfs_data data = {
          .lower_path_name = (char*)dev_name,
          .trfs_option = raw_data,
  };
  atomic_set(&trecord_id, -1);

	return mount_nodev(fs_type, flags, (void*)&data,
			   trfs_read_super);
}

static struct file_system_type trfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= TRFS_NAME,
	.mount		= trfs_mount,
	.kill_sb	= generic_shutdown_super,
	.fs_flags	= 0,
};
MODULE_ALIAS_FS(TRFS_NAME);

static int __init init_trfs_fs(void)
{
	int err;

	pr_info("Registering trfs " TRFS_VERSION "\n");

	err = trfs_init_inode_cache();
	if (err)
		goto out;
	err = trfs_init_dentry_cache();
	if (err)
		goto out;
	err = register_filesystem(&trfs_fs_type);
out:
	if (err) {
		trfs_destroy_inode_cache();
		trfs_destroy_dentry_cache();
	}
	return err;
}

static void __exit exit_trfs_fs(void)
{
	trfs_destroy_inode_cache();
	trfs_destroy_dentry_cache();
	unregister_filesystem(&trfs_fs_type);
	pr_info("Completed trfs module unload\n");
}

MODULE_AUTHOR("Mukul Sharma"
	      "Stony Brook University");
MODULE_DESCRIPTION("Trfs " TRFS_VERSION
		   " trfs filesystem");
MODULE_LICENSE("GPL");

module_init(init_trfs_fs);
module_exit(exit_trfs_fs);
