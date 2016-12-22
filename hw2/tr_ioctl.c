#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <../fs/trfs/trfs.h>

#include "tr_ioctl.h"

#define FIRST_MINOR 0
#define MINOR_CNT 1

static dev_t trace_dev;
static struct cdev trace_cdev;
static struct class *trace_class;

 
static int tr_open(struct inode *i, struct file *f) {
	return 0;
}
static int tr_close(struct inode *i, struct file *f) {
	return 0;
}

static long  tr_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
  tr_arg_t trctl;
  struct filename *mnt_path;
  struct file *mnt_filp;
  struct inode *mnt_inode;
  struct super_block *sb;

	switch (cmd) {
		case TR_GET_BMAP:
			if (copy_from_user(&trctl, (tr_arg_t *)arg, sizeof(tr_arg_t))) {
				return -EACCES;
			}

      mnt_path = getname_kernel((const char __user*)trctl.mntpath);
      if (IS_ERR(mnt_path)) {
          return PTR_ERR(mnt_path);
      }

      mnt_filp = filp_open(mnt_path->name, O_RDONLY, 0);
      if (IS_ERR(mnt_filp)) {
          putname(mnt_path);
          return PTR_ERR(mnt_filp);
      }

      mnt_inode = mnt_filp->f_inode;
      sb = mnt_inode->i_sb;
      if (strcmp(sb->s_type->name, TRFS_NAME) != 0) {
          return -EINVAL;
      }
      /* Get bitmap value */
      mutex_lock(&(TRFS_SB(sb)->trfs_lock));
      trctl.bitmap = TRFS_SB(sb)->umask;
      mutex_unlock(&(TRFS_SB(sb)->trfs_lock));
      printk("bitmask:%lu, sb:%p\n",TRFS_SB(sb)->umask, sb);

      putname(mnt_path);
      filp_close(mnt_filp, NULL);

      /* return to user */
			if (copy_to_user((tr_arg_t *)arg, &trctl, sizeof(tr_arg_t))) {
				return -EACCES;
			}
			break;
		case TR_SET_BMAP:
			if (copy_from_user(&trctl, (tr_arg_t *)arg, sizeof(tr_arg_t))) {
				return -EACCES;
			}
      
      mnt_path = getname_kernel((const char __user*)trctl.mntpath);
      if (IS_ERR(mnt_path)) {
          return PTR_ERR(mnt_path);
      }
      
      mnt_filp = filp_open(mnt_path->name, O_RDONLY, 0);
      if (IS_ERR(mnt_filp)) {
          putname(mnt_path);
          return PTR_ERR(mnt_filp);
      }

      mnt_inode = mnt_filp->f_inode;
      sb = mnt_inode->i_sb;
      if (strcmp(sb->s_type->name, TRFS_NAME) != 0) {
          return -EINVAL;
      }
      /* Set the bitmap value */
      mutex_lock(&(TRFS_SB(sb)->trfs_lock));
      TRFS_SB(sb)->umask = trctl.bitmap;
      mutex_unlock(&(TRFS_SB(sb)->trfs_lock));

      putname(mnt_path);
      filp_close(mnt_filp , NULL);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}


static struct file_operations tr_fops = {
	.owner = THIS_MODULE,
	.open = tr_open,
	.release = tr_close,
	.unlocked_ioctl = tr_ioctl,
};

static int __init tr_ioctl_init(void) {
	int ret;
	struct device *dev_ret;

	if ((ret = alloc_chrdev_region(&trace_dev, FIRST_MINOR, MINOR_CNT,
       "tr_ioctl")) < 0) {
		return ret;
	}
  
	cdev_init(&trace_cdev, &tr_fops);

	if ((ret = cdev_add(&trace_cdev, trace_dev, MINOR_CNT)) < 0) {
		return ret;
	}
	
	if (IS_ERR(trace_class = class_create(THIS_MODULE, "char"))) {
		cdev_del(&trace_cdev);
		unregister_chrdev_region(trace_dev, MINOR_CNT);
		return PTR_ERR(trace_class);
	}

	if (IS_ERR(dev_ret = device_create(trace_class, NULL, trace_dev, NULL, TRCTL_FILE))) {
    printk("ioctl file was not created\n");
		class_destroy(trace_class);
		cdev_del(&trace_cdev);
		unregister_chrdev_region(trace_dev, MINOR_CNT);
		return PTR_ERR(dev_ret);
	}

	return 0;
}

static void __exit tr_ioctl_exit(void) {
	device_destroy(trace_class, trace_dev);
	class_destroy(trace_class);
	cdev_del(&trace_cdev);
	unregister_chrdev_region(trace_dev, MINOR_CNT);
}

module_init(tr_ioctl_init);
module_exit(tr_ioctl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mukul Sharma");
MODULE_DESCRIPTION("trace ioctl");
