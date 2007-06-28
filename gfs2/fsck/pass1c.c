/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
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

#include "libgfs2.h"
#include "fsck.h"
#include "util.h"
#include "metawalk.h"

static int remove_eattr_entry(struct gfs2_sbd *sdp,
							  struct gfs2_buffer_head *leaf_bh,
							  struct gfs2_ea_header *curr,
							  struct gfs2_ea_header *prev)
{
	log_warn("Removing EA located in block #%"PRIu64" (0x%" PRIx64 ").\n",
			 leaf_bh->b_blocknr, leaf_bh->b_blocknr);
	if(!prev)
		curr->ea_type = GFS2_EATYPE_UNUSED;
	else {
		prev->ea_rec_len =
			cpu_to_be32(be32_to_cpu(curr->ea_rec_len) +
						be32_to_cpu(prev->ea_rec_len));
		if (curr->ea_flags & GFS2_EAFLAG_LAST)
			prev->ea_flags |= GFS2_EAFLAG_LAST;	
	}
	return 0;
}

int check_eattr_indir(struct gfs2_inode *ip, uint64_t block,
		      uint64_t parent, struct gfs2_buffer_head **bh,
		      void *private)
{
	int *update = (int *) private;
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_block_query q;
	struct gfs2_buffer_head *indir_bh = NULL;

	if(gfs2_check_range(sbp, block)) {
		log_err("Extended attributes indirect block out of range...removing\n");
		ip->i_di.di_eattr = 0;
		*update = 1;
		return 1;
	}
	else if (gfs2_block_check(bl, block, &q)) {
		stack;
		return -1;
	}
	else if(q.block_type != gfs2_indir_blk) {
		log_err("Extended attributes indirect block invalid...removing\n");
		ip->i_di.di_eattr = 0;
		*update = 1;
		return 1;
	}
	else
		indir_bh = bread(sbp, block);

	*bh = indir_bh;
	return 0;
}
int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
		     uint64_t parent, struct gfs2_buffer_head **bh, void *private)
{
	int *update = (int *) private;
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_block_query q;
	struct gfs2_buffer_head *leaf_bh;

	if(gfs2_check_range(sbp, block)) {
		log_err("Extended attributes block out of range...removing\n");
		ip->i_di.di_eattr = 0;
		*update = 1;
		return 1;
	}
	else if (gfs2_block_check(bl, block, &q)) {
		stack;
		return -1;
	}
	else if(q.block_type != gfs2_meta_eattr) {
		log_err("Extended attributes block invalid...removing\n");
		ip->i_di.di_eattr = 0;
		*update = 1;
		return 1;
	}
	else 
		leaf_bh = bread(sbp, block);

	*bh = leaf_bh;
	return 0;
}

static int check_eattr_entry(struct gfs2_inode *ip,
			     struct gfs2_buffer_head *leaf_bh,
			     struct gfs2_ea_header *ea_hdr,
			     struct gfs2_ea_header *ea_hdr_prev,
			     void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	char ea_name[256];
	uint32_t offset = (uint32_t)(((unsigned long)ea_hdr) -
			                  ((unsigned long)leaf_bh->b_data));
	uint32_t max_size = sdp->sd_sb.sb_bsize;

	if(!ea_hdr->ea_name_len){
		log_err("EA has name length == 0\n");
		ea_hdr->ea_flags |= GFS2_EAFLAG_LAST;
		ea_hdr->ea_rec_len = cpu_to_be32(max_size - offset);
		if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}
	if(offset + be32_to_cpu(ea_hdr->ea_rec_len) > max_size){
		log_err("EA rec length too long\n");
		ea_hdr->ea_flags |= GFS2_EAFLAG_LAST;
		ea_hdr->ea_rec_len = cpu_to_be32(max_size - offset);
		if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}
	if(offset + be32_to_cpu(ea_hdr->ea_rec_len) == max_size &&
	   (ea_hdr->ea_flags & GFS2_EAFLAG_LAST) == 0){
		log_err("last EA has no last entry flag\n");
		ea_hdr->ea_flags |= GFS2_EAFLAG_LAST;
		if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}
	if(!ea_hdr->ea_name_len){
		log_err("EA has name length == 0\n");
		if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs2_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS2_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		log_err("EA (%s) type is invalid (%d > %d).\n",
			ea_name, ea_hdr->ea_type, GFS2_EATYPE_LAST);
		if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}

	if(ea_hdr->ea_num_ptrs){
		uint32_t avail_size;
		int max_ptrs;

		avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs){
			log_err("EA (%s) has incorrect number of pointers.\n", ea_name);
			log_err("  Required:  %d\n"
				"  Reported:  %d\n",
				max_ptrs, ea_hdr->ea_num_ptrs);
			if(remove_eattr_entry(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
				stack;
				return -1;
			}
			return 1;
		} else {
			log_debug("  Pointers Required: %d\n  Pointers Reported: %d\n",
					  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

int check_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_ptr,
			 struct gfs2_buffer_head *leaf_bh,
			 struct gfs2_ea_header *ea_hdr,
			 struct gfs2_ea_header *ea_hdr_prev,
			 void *private)
{
	struct gfs2_block_query q;
	struct gfs2_sbd *sbp = ip->i_sbd;
	if(gfs2_block_check(bl, be64_to_cpu(*ea_ptr), &q)) {
		stack;
		return -1;
	}
	if(q.block_type != gfs2_meta_eattr) {
		if(remove_eattr_entry(sbp, leaf_bh, ea_hdr, ea_hdr_prev)){
			stack;
			return -1;
		}
		return 1;
	}
	return 0;
}

/* Go over all inodes with extended attributes and verify the EAs are
 * valid */
int pass1c(struct gfs2_sbd *sbp)
{
	uint64_t block_no = 0;
	struct gfs2_buffer_head *bh;
	struct gfs2_inode *ip = NULL;
	int update = 0;
	struct metawalk_fxns pass1c_fxns = { 0 };
	int error = 0;

	pass1c_fxns.check_eattr_indir = &check_eattr_indir;
	pass1c_fxns.check_eattr_leaf = &check_eattr_leaf;
	pass1c_fxns.check_eattr_entry = &check_eattr_entry;
	pass1c_fxns.check_eattr_extentry = &check_eattr_extentry;
	pass1c_fxns.private = (void *) &update;

	log_info("Looking for inodes containing ea blocks...\n");
	while (!gfs2_find_next_block_type(bl, gfs2_eattr_block, &block_no)) {

		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		log_info("EA in inode %"PRIu64" (0x%" PRIx64 ")\n", block_no,
				 block_no);
		bh = bread(sbp, block_no);
		ip = inode_get(sbp, bh);

		log_debug("Found eattr at %"PRIu64" (0x%" PRIx64 ")\n",
				  ip->i_di.di_eattr, ip->i_di.di_eattr);
		/* FIXME: Handle walking the eattr here */
		error = check_inode_eattr(ip, &pass1c_fxns);
		if(error < 0) {
			stack;
			return -1;
		}

		if(update)
			gfs2_dinode_out(&ip->i_di, bh->b_data);

		free(ip);
		brelse(bh, update);

		block_no++;
	}
	return 0;
}
