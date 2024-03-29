/*
 * Copyright (c) 2016	   Mukul Sharma
 * Copyright (c) 2003-2015 Stony Brook University
 * Copyright (c) 2003-2015 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "trfs.h"
#include <linux/atomic.h>
#include <linux/unistd.h>

static int trfs_create(struct inode *dir, struct dentry *dentry,
			 umode_t mode, bool want_excl)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_create(d_inode(lower_parent_dentry), lower_dentry, mode,
			 want_excl);
	if (err)
		goto out;
	err = trfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, trfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
  printk("*%s*\n", __func__);
  mutex_lock(&(TRFS_SB(dir->i_sb)->trfs_lock));
  record_type = TR_FCREAT & TRFS_SB(dir->i_sb)->umask;
  if(fill_record(record_type, dentry, 0, mode, err, NULL, 0, &tr_rec) == 0) {
    append_record(dir->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(dir->i_sb)->trfs_lock));

	unlock_dir(lower_parent_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int trfs_link(struct dentry *old_dentry, struct inode *dir,
		       struct dentry *new_dentry)
{
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_dir_dentry;
	u64 file_size_save;
	int err;
	struct path lower_old_path, lower_new_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	file_size_save = i_size_read(d_inode(old_dentry));
	trfs_get_lower_path(old_dentry, &lower_old_path);
	trfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_dir_dentry = lock_parent(lower_new_dentry);

	err = vfs_link(lower_old_dentry, d_inode(lower_dir_dentry),
		       lower_new_dentry, NULL);
	if (err || !d_inode(lower_new_dentry))
		goto out;

	err = trfs_interpose(new_dentry, dir->i_sb, &lower_new_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, d_inode(lower_new_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_new_dentry));
	set_nlink(d_inode(old_dentry),
		  trfs_lower_inode(d_inode(old_dentry))->i_nlink);
	i_size_write(d_inode(new_dentry), file_size_save);
out:
  printk("*%s*\n", __func__);
  mutex_lock(&(TRFS_SB(d_inode(old_dentry)->i_sb)->trfs_lock));
  record_type = TR_LINK & TRFS_SB(d_inode(old_dentry)->i_sb)->umask;
  if (fill_record(record_type, old_dentry, 0, 0, err, 
                  (void*)new_dentry, 0, &tr_rec) == 0) {
    append_record(d_inode(old_dentry)->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(d_inode(old_dentry)->i_sb)->trfs_lock));

	unlock_dir(lower_dir_dentry);
	trfs_put_lower_path(old_dentry, &lower_old_path);
	trfs_put_lower_path(new_dentry, &lower_new_path);
	return err;
}

static int trfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;
	struct dentry *lower_dentry;
	struct inode *lower_dir_inode = trfs_lower_inode(dir);
	struct dentry *lower_dir_dentry;
	struct path lower_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_unlink(lower_dir_inode, lower_dentry, NULL);

	/*
	 * Note: unlinking on top of NFS can cause silly-renamed files.
	 * Trying to delete such files results in EBUSY from NFS
	 * below.  Silly-renamed files will get deleted by NFS later on, so
	 * we just need to detect them here and treat such EBUSY errors as
	 * if the upper file was successfully deleted.
	 */
	if (err == -EBUSY && lower_dentry->d_flags & DCACHE_NFSFS_RENAMED)
		err = 0;
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_inode);
	fsstack_copy_inode_size(dir, lower_dir_inode);
	set_nlink(d_inode(dentry),
		  trfs_lower_inode(d_inode(dentry))->i_nlink);
	d_inode(dentry)->i_ctime = dir->i_ctime;
	d_drop(dentry); /* this is needed, else LTP fails (VFS won't do it) */
out:
  printk("*%s*\n", __func__);
  mutex_lock(&(TRFS_SB(dir->i_sb)->trfs_lock));
  record_type = TR_UNLINK & TRFS_SB(dir->i_sb)->umask;
  if (fill_record(record_type, dentry, 0, 0, err, NULL, 0, &tr_rec) == 0) {
    append_record(dir->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(dir->i_sb)->trfs_lock));

	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int trfs_symlink(struct inode *dir, struct dentry *dentry,
			  const char *symname)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_symlink(d_inode(lower_parent_dentry), lower_dentry, symname);
	if (err)
		goto out;
	err = trfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, trfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
  /*TODO: remove all printk once stable*/
  printk("*%s* symlink name:%s\n", __func__, symname);
  mutex_lock(&(TRFS_SB(d_inode(dentry)->i_sb)->trfs_lock));
  record_type = TR_SYMLINK & TRFS_SB(d_inode(dentry)->i_sb)->umask;
  if (fill_record(record_type, dentry, 0, 0, err, (void*)symname, strlen(symname),
                 &tr_rec) == 0) {
    append_record(d_inode(dentry)->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(d_inode(dentry)->i_sb)->trfs_lock));

	unlock_dir(lower_parent_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int trfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_mkdir(d_inode(lower_parent_dentry), lower_dentry, mode);
	if (err)
		goto out;

	err = trfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;

	fsstack_copy_attr_times(dir, trfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	/* update number of links on parent directory */
	set_nlink(dir, trfs_lower_inode(dir)->i_nlink);
out:
  printk("*%s* : %p\n", __func__, dir->i_sb);
  mutex_lock(&(TRFS_SB(dir->i_sb)->trfs_lock));
  record_type = TR_DCREAT & TRFS_SB(dir->i_sb)->umask;
  if (fill_record(record_type, dentry, 0, mode, err,
                   NULL, 0, &tr_rec) == 0) {
    append_record(dir->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(dir->i_sb)->trfs_lock));

	unlock_dir(lower_parent_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int trfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int err;
	struct path lower_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_rmdir(d_inode(lower_dir_dentry), lower_dentry);
	if (err)
		goto out;

	d_drop(dentry);	/* drop our dentry on success (why not VFS's job?) */
	if (d_inode(dentry))
		clear_nlink(d_inode(dentry));
	fsstack_copy_attr_times(dir, d_inode(lower_dir_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_dir_dentry));
	set_nlink(dir, d_inode(lower_dir_dentry)->i_nlink);

out:
  printk("*%s*\n", __func__);
  mutex_lock(&(TRFS_SB(dir->i_sb)->trfs_lock));
  record_type = TR_UNLINK & TRFS_SB(dir->i_sb)->umask;
  if (fill_record(record_type, dentry, 0, 0, err, NULL, 0, &tr_rec) == 0) {
    append_record(dir->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(dir->i_sb)->trfs_lock));

	unlock_dir(lower_dir_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int trfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
			dev_t dev)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_mknod(d_inode(lower_parent_dentry), lower_dentry, mode, dev);
	if (err)
		goto out;

	err = trfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, trfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

/*
 * The locking rules in trfs_rename are complex.  We could use a simpler
 * superblock-level name-space lock for renames and copy-ups.
 */
static int trfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	int err = 0;
	struct dentry *lower_old_dentry = NULL;
	struct dentry *lower_new_dentry = NULL;
	struct dentry *lower_old_dir_dentry = NULL;
	struct dentry *lower_new_dir_dentry = NULL;
	struct dentry *trap = NULL;
	struct path lower_old_path, lower_new_path;
  struct tr_record tr_rec;
  rec_type_t record_type;

	trfs_get_lower_path(old_dentry, &lower_old_path);
	trfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);

	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	/* source should not be ancestor of target */
	if (trap == lower_old_dentry) {
		err = -EINVAL;
		goto out;
	}
	/* target should not be ancestor of source */
	if (trap == lower_new_dentry) {
		err = -ENOTEMPTY;
		goto out;
	}

	err = vfs_rename(d_inode(lower_old_dir_dentry), lower_old_dentry,
			 d_inode(lower_new_dir_dentry), lower_new_dentry,
			 NULL, 0);
	if (err)
		goto out;

	fsstack_copy_attr_all(new_dir, d_inode(lower_new_dir_dentry));
	fsstack_copy_inode_size(new_dir, d_inode(lower_new_dir_dentry));
	if (new_dir != old_dir) {
		fsstack_copy_attr_all(old_dir,
				      d_inode(lower_old_dir_dentry));
		fsstack_copy_inode_size(old_dir,
					d_inode(lower_old_dir_dentry));
	}

out:
  printk("*%s*\n", __func__);
  mutex_lock(&(TRFS_SB(d_inode(old_dentry)->i_sb)->trfs_lock));
  record_type = TR_RENAME & TRFS_SB(d_inode(old_dentry)->i_sb)->umask;
  if (fill_record(record_type, old_dentry, 0, 0,
                   err, (void*)new_dentry, 0, &tr_rec) == 0) {
    append_record(d_inode(old_dentry)->i_sb, &tr_rec);
  }
  mutex_unlock(&(TRFS_SB(d_inode(old_dentry)->i_sb)->trfs_lock));

	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	dput(lower_old_dir_dentry);
	dput(lower_new_dir_dentry);
	trfs_put_lower_path(old_dentry, &lower_old_path);
	trfs_put_lower_path(new_dentry, &lower_new_path);
	return err;
}

static int trfs_readlink(struct dentry *dentry, char __user *buf, int bufsiz)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op ||
	    !d_inode(lower_dentry)->i_op->readlink) {
		err = -EINVAL;
		goto out;
	}

	err = d_inode(lower_dentry)->i_op->readlink(lower_dentry,
						    buf, bufsiz);
	if (err < 0)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry), d_inode(lower_dentry));

out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static const char *trfs_get_link(struct dentry *dentry, struct inode *inode,
				   struct delayed_call *done)
{
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	/* This is freed by the put_link method assuming a successful call. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		buf = ERR_PTR(-ENOMEM);
		return buf;
	}

	/* read the symlink, and then we will follow it */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = trfs_readlink(dentry, buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = ERR_PTR(err);
	} else {
		buf[err] = '\0';
	}
	set_delayed_call(done, kfree_link, buf);
	return buf;
}

static int trfs_permission(struct inode *inode, int mask)
{
	struct inode *lower_inode;
	int err;

	lower_inode = trfs_lower_inode(inode);
	err = inode_permission(lower_inode, mask);
	return err;
}
/*TODO: for set get remove attrs*/
static int trfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct iattr lower_ia;

	inode = d_inode(dentry);

	/*
	 * Check if user has permission to change inode.  We don't check if
	 * this user can change the lower inode: that should happen when
	 * calling notify_change on the lower inode.
	 */
	err = inode_change_ok(inode, ia);
	if (err)
		goto out_err;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = trfs_lower_inode(inode);

	/* prepare our own lower struct iattr (with the lower file) */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = trfs_lower_file(ia->ia_file);

	/*
	 * If shrinking, first truncate upper level to cancel writing dirty
	 * pages beyond the new eof; and also if its' maxbytes is more
	 * limiting (fail with -EFBIG before making any change to the lower
	 * level).  There is no need to vmtruncate the upper level
	 * afterwards in the other cases: we fsstack_copy_inode_size from
	 * the lower level.
	 */
	if (ia->ia_valid & ATTR_SIZE) {
		err = inode_newsize_ok(inode, ia->ia_size);
		if (err)
			goto out;
		truncate_setsize(inode, ia->ia_size);
	}

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	/* notify the (possibly copied-up) lower inode */
	/*
	 * Note: we use d_inode(lower_dentry), because lower_inode may be
	 * unlinked (no inode->i_sb and i_ino==0.  This happens if someone
	 * tries to open(), unlink(), then ftruncate() a file.
	 */
	inode_lock(d_inode(lower_dentry));
	err = notify_change(lower_dentry, &lower_ia, /* note: lower_ia */
			    NULL);
	inode_unlock(d_inode(lower_dentry));
	if (err)
		goto out;

	/* get attributes from the lower inode */
	fsstack_copy_attr_all(inode, lower_inode);
	/*
	 * Not running fsstack_copy_inode_size(inode, lower_inode), because
	 * VFS should update our inode size, and notify_change on
	 * lower_inode should update its size.
	 */

out:
	trfs_put_lower_path(dentry, &lower_path);
out_err:
	return err;
}

static int trfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			  struct kstat *stat)
{
	int err;
	struct kstat lower_stat;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	err = vfs_getattr(&lower_path, &lower_stat);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry),
			      d_inode(lower_path.dentry));
	generic_fillattr(d_inode(dentry), stat);
	stat->blocks = lower_stat.blocks;
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int
trfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	int err; struct dentry *lower_dentry;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op->setxattr) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_setxattr(lower_dentry, name, value, size, flags);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry),
			      d_inode(lower_path.dentry));
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static ssize_t
trfs_getxattr(struct dentry *dentry, const char *name, void *buffer,
		size_t size)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op->getxattr) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_getxattr(lower_dentry, name, buffer, size);
	if (err)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry),
				d_inode(lower_path.dentry));
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static ssize_t
trfs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op->listxattr) {
		err = -EOPNOTSUPP;
		goto out;
	}
	err = vfs_listxattr(lower_dentry, buffer, buffer_size);
	if (err)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry),
				d_inode(lower_path.dentry));
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static int
trfs_removexattr(struct dentry *dentry, const char *name)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op ||
	    !d_inode(lower_dentry)->i_op->removexattr) {
		err = -EINVAL;
		goto out;
	}
	err = vfs_removexattr(lower_dentry, name);
	if (err)
		goto out;
	fsstack_copy_attr_all(d_inode(dentry),
			      d_inode(lower_path.dentry));
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}
const struct inode_operations trfs_symlink_iops = {
	.readlink	= trfs_readlink,
	.permission	= trfs_permission,
	.setattr	= trfs_setattr,
	.getattr	= trfs_getattr,
	.get_link	= trfs_get_link,
	.setxattr	= trfs_setxattr,
	.getxattr	= trfs_getxattr,
	.listxattr	= trfs_listxattr,
	.removexattr	= trfs_removexattr,
};

const struct inode_operations trfs_dir_iops = {
	.create		= trfs_create,
	.lookup		= trfs_lookup,
	.link		= trfs_link,
	.unlink		= trfs_unlink,
	.symlink	= trfs_symlink,
	.mkdir		= trfs_mkdir,
	.rmdir		= trfs_rmdir,
	.mknod		= trfs_mknod,
	.rename		= trfs_rename,
	.permission	= trfs_permission,
	.setattr	= trfs_setattr,
	.getattr	= trfs_getattr,
	.setxattr	= trfs_setxattr,
	.getxattr	= trfs_getxattr,
	.listxattr	= trfs_listxattr,
	.removexattr	= trfs_removexattr,
};

const struct inode_operations trfs_main_iops = {
	.permission	= trfs_permission,
	.setattr	= trfs_setattr,
	.getattr	= trfs_getattr,
	.setxattr	= trfs_setxattr,
	.getxattr	= trfs_getxattr,
	.listxattr	= trfs_listxattr,
	.removexattr	= trfs_removexattr,
};
