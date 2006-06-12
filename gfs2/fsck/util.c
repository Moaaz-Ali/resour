/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#include "libgfs2.h"
#include "fs_bits.h"
#include "util.h"

/**
 * compute_height
 * @sdp:
 * @sz:
 *
 */
int compute_height(struct gfs2_sbd *sdp, uint64_t sz)
{
	unsigned int height;
	uint64_t space, old_space;
	unsigned int bsize = sdp->sd_sb.sb_bsize;
	
	if (sz <= (bsize - sizeof(struct gfs2_dinode)))
		return 0;
	
	height = 1;
	space = sdp->sd_diptrs * bsize;
	
	while (sz > space) {
		old_space = space;
		
		height++;
		space *= sdp->sd_inptrs;
		
		if (space / sdp->sd_inptrs != old_space ||
			space % sdp->sd_inptrs != 0)
			break;
	}
	return height;
}

/* Put out a warm, fuzzy message every second so the customer */
/* doesn't think we hung.  (This may take a long time).       */
void warm_fuzzy_stuff(uint64_t block)
{
	static struct timeval tv;
	static uint32_t seconds = 0;

	gettimeofday(&tv, NULL);
	if (!seconds)
        seconds = tv.tv_sec;
	if (tv.tv_sec - seconds) {
		uint64_t percent;

		seconds = tv.tv_sec;
		percent = (block * 100) / last_fs_block;
		printf("\r%" PRIu64 " percent complete.\r", percent);
		fflush(stdout);
	}
}

const char *block_type_string(struct gfs2_block_query *q)
{
	const char *blktyp[] = {"free", "used", "indirect data", "inode",
							"file", "symlink", "block dev", "char dev",
							"fifo", "socket", "dir leaf", "journ data",
							"other meta", "eattribute", "unused",
							"invalid"};
	if (q->block_type < 16)
		return (blktyp[q->block_type]);
	return blktyp[15];
}
