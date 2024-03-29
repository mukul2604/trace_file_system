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

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int trfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct path lower_path;
	struct dentry *lower_dentry;
	int err = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	trfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!(lower_dentry->d_flags & DCACHE_OP_REVALIDATE))
		goto out;
	err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
out:
	trfs_put_lower_path(dentry, &lower_path);
	return err;
}

static void trfs_d_release(struct dentry *dentry)
{
	/* release and reset the lower paths */
	trfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

const struct dentry_operations trfs_dops = {
	.d_revalidate	= trfs_d_revalidate,
	.d_release	= trfs_d_release,
};
