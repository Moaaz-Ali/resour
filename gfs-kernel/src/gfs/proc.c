/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "gfs.h"
#include "proc.h"
#include "super.h"

struct list_head gfs_fs_list;
struct semaphore gfs_fs_lock;
char *gfs_proc_margs;
spinlock_t gfs_proc_margs_lock;
spinlock_t req_lock;

/**
 * gfs_proc_fs_add - Add a FS to the list of mounted FSs
 * @sdp:
 *
 */

void
gfs_proc_fs_add(struct gfs_sbd *sdp)
{
	down(&gfs_fs_lock);
	list_add(&sdp->sd_list, &gfs_fs_list);
	up(&gfs_fs_lock);
}

/**
 * gfs_proc_fs_del - Remove a FS from the list of mounted FSs
 * @sdp:
 *
 */

void
gfs_proc_fs_del(struct gfs_sbd *sdp)
{
	down(&gfs_fs_lock);
	list_del(&sdp->sd_list);
	up(&gfs_fs_lock);
}

/**
 * do_list - Copy the list of mountes FSs to userspace
 * @user_buf:
 * @size:
 *
 * @Returns: -errno, or the number of bytes copied to userspace
 */

static ssize_t
do_list(char *user_buf, size_t size)
{
	struct list_head *tmp;
	struct gfs_sbd *sdp = NULL;
	unsigned int s = 0, o = 0;
	char num[21];
	char *buf;
	int error = 0;

	down(&gfs_fs_lock);

	for (tmp = gfs_fs_list.next; tmp != &gfs_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs_sbd, sd_list);
		s += sprintf(num, "%lu", (unsigned long)sdp) +
			strlen(sdp->sd_vfs->s_id) +
			strlen(sdp->sd_fsname) + 3;
	}

	if (!s)
		goto out;

	error = -EFBIG;
	if (s > size)
		goto out;

	error = -ENOMEM;
	buf = kmalloc(s + 1, GFP_KERNEL);
	if (!buf)
		goto out;

	for (tmp = gfs_fs_list.next; tmp != &gfs_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs_sbd, sd_list);
		o += sprintf(buf + o, "%lu %s %s\n",
			     (unsigned long)sdp, sdp->sd_vfs->s_id, sdp->sd_fsname);
	}

	GFS_ASSERT_SBD(o <= s, sdp,);

	if (copy_to_user(user_buf, buf, o))
		error = -EFAULT;
	else
		error = o;

	kfree(buf);

 out:
	up(&gfs_fs_lock);

	return error;
}

/**
 * find_argument - 
 * @p:
 *
 * Returns:
 */

static char *
find_argument(char *p)
{
	char *p2;

	while (*p == ' ' || *p == '\n')
		p++;
	if (!*p)
		return NULL;
	for (p2 = p; *p2; p2++) /* do nothing */;
	p2--;
	while (*p2 == ' ' || *p2 == '\n')
		*p2-- = 0;

	return p;
}

/**
 * do_freeze - freeze a filesystem
 * @p: the freeze command
 *
 * Returns: errno
 */

static int
do_freeze(char *p)
{
	struct list_head *tmp;
	struct gfs_sbd *sdp;
	char num[21];
	int error = 0;

	p = find_argument(p + 6);
	if (!p)
		return -ENOENT;

	down(&gfs_fs_lock);

	for (tmp = gfs_fs_list.next; tmp != &gfs_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs_fs_list)
		error = -ENOENT;
	else
		error = gfs_freeze_fs(sdp);

	up(&gfs_fs_lock);

	return error;
}

/**
 * do_unfreeze - unfreeze a filesystem
 * @p: the unfreeze command
 *
 * Returns: errno
 */

static int
do_unfreeze(char *p)
{
	struct list_head *tmp;
	struct gfs_sbd *sdp;
	char num[21];
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		return -ENOENT;

	down(&gfs_fs_lock);

	for (tmp = gfs_fs_list.next; tmp != &gfs_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs_fs_list)
		error = -ENOENT;
	else
		gfs_unfreeze_fs(sdp);

	up(&gfs_fs_lock);

	return error;
}

/**
 * do_margs - Pass in mount arguments
 * @p: the margs command
 *
 * Returns: errno
 */

static int
do_margs(char *p)
{
	char *new_buf, *old_buf;

	p = find_argument(p + 5);
	if (!p)
		return -ENOENT;

	new_buf = kmalloc(strlen(p) + 1, GFP_KERNEL);
	if (!new_buf)
		return -ENOMEM;
	strcpy(new_buf, p);

	spin_lock(&gfs_proc_margs_lock);
	old_buf = gfs_proc_margs;
	gfs_proc_margs = new_buf;
	spin_unlock(&gfs_proc_margs_lock);

	if (old_buf)
		kfree(old_buf);

	return 0;
}

/**
 * gfs_proc_write - take a command from userspace
 * @file:
 * @buf:
 * @size:
 * @offset:
 *
 * Returns: -errno or the number of bytes taken
 */

static ssize_t
gfs_proc_write(struct file *file, const char *buf, size_t size, loff_t *offset)
{
	char *p;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	if (p)
		kfree(p);

	if (!size)
		return -EINVAL;

	p = kmalloc(size + 1, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p[size] = 0;

	if (copy_from_user(p, buf, size)) {
		kfree(p);
		return -EFAULT;
	}

	spin_lock(&req_lock);
	file->private_data = p;
	spin_unlock(&req_lock);

	return size;
}

/**
 * gfs_proc_read - return the results of a command
 * @file:
 * @buf:
 * @size:
 * @offset:
 *
 * Returns: -errno or the number of bytes returned
 */

static ssize_t
gfs_proc_read(struct file *file, char *buf, size_t size, loff_t *offset)
{
	char *p;
	int error;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	if (!p)
		return -ENOENT;

	if (!size) {
		kfree(p);
		return -EINVAL;
	}

	if (strncmp(p, "list", 4) == 0)
		error = do_list(buf, size);
	else if (strncmp(p, "freeze", 6) == 0)
		error = do_freeze(p);
	else if (strncmp(p, "unfreeze", 8) == 0)
		error = do_unfreeze(p);
	else if (strncmp(p, "margs", 5) == 0)
		error = do_margs(p);
	else
		error = -ENOSYS;

	kfree(p);

	return error;
}

/**
 * gfs_proc_close - free any mismatches writes
 * @inode:
 * @file:
 *
 * Returns: 0
 */

static int
gfs_proc_close(struct inode *inode, struct file *file)
{
	if (file->private_data)
		kfree(file->private_data);
	return 0;
}

static struct file_operations gfs_proc_fops =
{
	.owner = THIS_MODULE,
	.write = gfs_proc_write,
	.read = gfs_proc_read,
	.release = gfs_proc_close,
};

/**
 * gfs_proc_init - initialize GFS' proc interface
 *
 */

void
gfs_proc_init(void)
{
	struct proc_dir_entry *pde;

	INIT_LIST_HEAD(&gfs_fs_list);
	init_MUTEX(&gfs_fs_lock);
	gfs_proc_margs = NULL;
	spin_lock_init(&gfs_proc_margs_lock);
	spin_lock_init(&req_lock);

	pde = create_proc_entry("fs/gfs", S_IFREG | 0600, NULL);
	if (pde) {
		pde->owner = THIS_MODULE;
		pde->proc_fops = &gfs_proc_fops;
	}
}

/**
 * gfs_proc_uninit - uninitialize GFS' proc interface
 *
 */

void
gfs_proc_uninit(void)
{
	if (gfs_proc_margs)
		kfree(gfs_proc_margs);
	remove_proc_entry("fs/gfs", NULL);
}

