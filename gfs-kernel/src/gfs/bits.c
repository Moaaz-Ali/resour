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

/*
 * These routines are used by the resource group routines (rgrp.c)
 * to keep track of block allocation.  Each block is represented by two
 * bits.  One bit indicates whether or not the block is used.  (1=used,
 * 0=free)  The other bit indicates whether or not the block contains a
 * dinode or not.  (1=dinode, 0=data block) So, each byte represents
 * GFS_NBBY (i.e. 4) blocks.  
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "bits.h"

static const char valid_change[16] = {
	        /* current */
	/* n */ 0, 1, 1, 1,
	/* e */ 1, 0, 0, 0,
	/* w */ 1, 0, 0, 1,
	        0, 0, 1, 0
};

/**
 * gfs_setbit - Set a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */

void
gfs_setbit(struct gfs_rgrpd *rgd,
	   unsigned char *buffer, unsigned int buflen,
	   uint32_t block, unsigned char new_state)
{
	ENTER(GFN_SETBIT)
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS_NBBY);
	bit = (block % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;

	gfs_assert(rgd->rd_sbd, byte < end,);

	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	if (valid_change[new_state * 4 + cur_state]) {
		*byte ^= cur_state << bit;
		*byte |= new_state << bit;
	} else
		gfs_consist_rgrpd(rgd);

	RET(GFN_SETBIT);
}

/**
 * gfs_testbit - test a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to read
 *
 */

unsigned char
gfs_testbit(struct gfs_rgrpd *rgd,
	    unsigned char *buffer, unsigned int buflen, uint32_t block)
{
	ENTER(GFN_TESTBIT)
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS_NBBY);
	bit = (block % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;

        gfs_assert(rgd->rd_sbd, byte < end,);

	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	RETURN(GFN_TESTBIT, cur_state);
}

/**
 * gfs_bitfit - Search an rgrp's bitmap buffer to find a bit-pair representing
 *       a block in a given allocation state.
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @goal: start search at this block's bit-pair (within @buffer)
 * @old_state: GFS_BLKST_XXX the state of the block we're looking for;
 *       bit 0 = alloc(1)/free(0), bit 1 = meta(1)/data(0)
 * 
 * Scope of @goal and returned block number is only within this bitmap buffer,
 *   not entire rgrp or filesystem.
 * @buffer will be offset from the actual beginning of a bitmap block buffer,
 *   skipping any header structures.
 *
 * Return: the block number (bitmap buffer scope) that was found
 */

uint32_t
gfs_bitfit(struct gfs_rgrpd *rgd,
	   unsigned char *buffer, unsigned int buflen,
	   uint32_t goal, unsigned char old_state)
{
	ENTER(GFN_BITFIT)
	unsigned char *byte, *end, alloc;
	uint32_t blk = goal;
	unsigned int bit;

	byte = buffer + (goal / GFS_NBBY);
	bit = (goal % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;
	alloc = (old_state & 1) ? 0 : 0x55;

	while (byte < end) {
		if ((*byte & 0x55) == alloc) {
			blk += (8 - bit) >> 1;

			bit = 0;
			byte++;

			continue;
		}

		if (((*byte >> bit) & GFS_BIT_MASK) == old_state)
			RETURN(GFN_BITFIT, blk);

		bit += GFS_BIT_SIZE;
		if (bit >= 8) {
			bit = 0;
			byte++;
		}

		blk++;
	}

	RETURN(GFN_BITFIT, BFITNOENT);
}

/**
 * gfs_bitcount - count the number of bits in a certain state
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @state: the state of the block we're looking for
 *
 * Returns: The number of bits
 */

uint32_t
gfs_bitcount(struct gfs_rgrpd *rgd,
	     unsigned char *buffer, unsigned int buflen,
	     unsigned char state)
{
	ENTER(GFN_BITCOUNT)
	unsigned char *byte = buffer;
	unsigned char *end = buffer + buflen;
	unsigned char state1 = state << 2;
	unsigned char state2 = state << 4;
	unsigned char state3 = state << 6;
	uint32_t count = 0;

	for (; byte < end; byte++) {
		if (((*byte) & 0x03) == state)
			count++;
		if (((*byte) & 0x0C) == state1)
			count++;
		if (((*byte) & 0x30) == state2)
			count++;
		if (((*byte) & 0xC0) == state3)
			count++;
	}

	RETURN(GFN_BITCOUNT, count);
}
