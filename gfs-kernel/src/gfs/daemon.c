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

#include "gfs.h"
#include "daemon.h"
#include "glock.h"
#include "log.h"
#include "quota.h"
#include "recovery.h"
#include "super.h"
#include "unlinked.h"

/**
 * gfs_scand - Look for cached glocks and inodes to toss from memory
 * @sdp: Pointer to GFS superblock
 *
 * One of these daemons runs, finding candidates to add to sd_reclaim_list.
 * See gfs_glockd()
 */

int
gfs_scand(void *data)
{
	ENTER(GFN_SCAND)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_scand");
	sdp->sd_scand_process = current;
	set_bit(SDF_SCAND_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_scand_internal(sdp);

		if (!test_bit(SDF_SCAND_RUN, &sdp->sd_flags))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(gfs_tune_get(sdp, gt_scand_secs) * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_SCAND, 0);
}

/**
 * gfs_glockd - Reclaim unused glock structures
 * @sdp: Pointer to GFS superblock
 *
 * One or more of these daemons run, reclaiming glocks on sd_reclaim_list.
 * sd_glockd_num says how many daemons are running now.
 * Number of daemons can be set by user, with num_glockd mount option.
 * See gfs_scand()
 */

int
gfs_glockd(void *data)
{
	ENTER(GFN_GLOCKD)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_glockd");
	set_bit(SDF_GLOCKD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		while (atomic_read(&sdp->sd_reclaim_count))
			gfs_reclaim_glock(sdp);

		if (!test_bit(SDF_GLOCKD_RUN, &sdp->sd_flags))
			break;

		{
			DECLARE_WAITQUEUE(__wait_chan, current);
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&sdp->sd_reclaim_wchan, &__wait_chan);
			if (!atomic_read(&sdp->sd_reclaim_count) &&
			    test_bit(SDF_GLOCKD_RUN, &sdp->sd_flags))
				schedule();
			remove_wait_queue(&sdp->sd_reclaim_wchan, &__wait_chan);
			set_current_state(TASK_RUNNING);
		}
	}

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_GLOCKD, 0);
}

/**
 * gfs_recoverd - Recover dead machine's journals
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_recoverd(void *data)
{
	ENTER(GFN_RECOVERD)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_recoverd");
	sdp->sd_recoverd_process = current;
	set_bit(SDF_RECOVERD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_check_journals(sdp);

		if (!test_bit(SDF_RECOVERD_RUN, &sdp->sd_flags))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(gfs_tune_get(sdp, gt_recoverd_secs) * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_RECOVERD, 0);
}

/**
 * gfs_logd - Update log tail as Active Items get flushed to in-place blocks
 * @sdp: Pointer to GFS superblock
 *
 * Also, periodically check to make sure that we're using the most recent
 * journal index.
 */

int
gfs_logd(void *data)
{
	ENTER(GFN_LOGD)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	struct gfs_holder ji_gh;

	daemonize("gfs_logd");
	sdp->sd_logd_process = current;
	set_bit(SDF_LOGD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		/* Advance the log tail */
		gfs_ail_empty(sdp);

		/* Check for latest journal index */
		if (time_after_eq(jiffies,
				  sdp->sd_jindex_refresh_time +
				  gfs_tune_get(sdp, gt_jindex_refresh_secs) * HZ)) {
			if (test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags) &&
			    !gfs_jindex_hold(sdp, &ji_gh))
				gfs_glock_dq_uninit(&ji_gh);
			sdp->sd_jindex_refresh_time = jiffies;
		}

		if (!test_bit(SDF_LOGD_RUN, &sdp->sd_flags))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(gfs_tune_get(sdp, gt_logd_secs) * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_LOGD, 0);
}

/**
 * gfs_quotad - Write cached quota changes into the quota file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_quotad(void *data)
{
	ENTER(GFN_QUOTAD)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	int error;

	daemonize("gfs_quotad");
	sdp->sd_quotad_process = current;
	set_bit(SDF_QUOTAD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		/* Update quota file */
		if (time_after_eq(jiffies,
				  sdp->sd_quota_sync_time +
				  gfs_tune_get(sdp, gt_quota_quantum) * HZ)) {
			error = gfs_quota_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				printk("GFS: fsid=%s: quotad: error = %d\n",
				       sdp->sd_fsname, error);
			sdp->sd_quota_sync_time = jiffies;
		}

		/* Clean up */
		gfs_quota_scan(sdp);

		if (!test_bit(SDF_QUOTAD_RUN, &sdp->sd_flags))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(gfs_tune_get(sdp, gt_quotad_secs) * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_QUOTAD, 0);
}

/**
 * gfs_inoded - Deallocate unlinked inodes
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_inoded(void *data)
{
	ENTER(GFN_INODED)
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_inoded");
	sdp->sd_inoded_process = current;
	set_bit(SDF_INODED_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_unlinked_dealloc(sdp);

		if (!test_bit(SDF_INODED_RUN, &sdp->sd_flags))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(gfs_tune_get(sdp, gt_inoded_secs) * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	RETURN(GFN_INODED, 0);
}
