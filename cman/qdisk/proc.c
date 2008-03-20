/**
  Copyright Red Hat, Inc. 2006

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.

  Author: Lon Hohberger <lhh at redhat.com>
 */
/**
  @file Quorum disk /proc/partition scanning functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <disk.h>
#include <errno.h>
#include <sys/types.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include "scandisk.h"

struct device_args {
	char *label;
	struct devnode *devnode;
	int sector_size;
	int flags;
	int count;
	int pad;
};


int
check_device(char *device, char *label, quorum_header_t *qh,
	     int flags)
{
	int ret = -1;
	quorum_header_t qh_local;
	target_info_t disk;

	if (!qh)
		qh = &qh_local;

	ret = qdisk_validate(device);
	if (ret < 0) {
		perror("qdisk_verify");
		return -1;
	}

	ret = qdisk_open(device, &disk);
	if (ret < 0) {
		perror("qdisk_open");
		return -1;
	}

	ret = -1;
	if (qdisk_read(&disk, OFFSET_HEADER, qh, sizeof(*qh)) == sizeof(*qh)) {
		swab_quorum_header_t(qh);
                if (qh->qh_magic == HEADER_MAGIC_NUMBER) {
			if (!label || !strcmp(qh->qh_cluster, label)) {
				ret = 0;
			}
                }
        }

	qh->qh_kernsz = disk.d_blksz;

	/* only flag now is 'strict device check'; i.e.,
	  "block size recorded must match kernel's reported size" */
	if (flags && qh->qh_version == VERSION_MAGIC_V2 &&
            disk.d_blksz != qh->qh_blksz) {
		ret = -1;
	}

	qdisk_close(&disk);

	return ret;
}


void
filter_devs(struct devnode *node, void *v_args)
{
	struct device_args *args = (struct device_args *)v_args;
	quorum_header_t qh;
	quorum_header_t *ret_qh = NULL;
	int ret;

	if (!node->sysfsattrs.sysfs)
		return;
	if (!node->devpath)
		return;
	if (node->sysfsattrs.holders)
		return;
	/* Qdiskd doesn't work on soft-raid */
	if (node->md > 0)
		return;

	ret = check_device(node->devpath->path, args->label, &qh, args->flags);
	if (ret == 0) {
		ret_qh = malloc(sizeof(qh));
		if (!ret_qh)
			return;
		memcpy(ret_qh, &qh, sizeof(qh));

		node->filter = (void *)ret_qh;
		if (!args->count) {
			args->devnode = node;
		}
		++args->count;
	}
}


char *
state_str(disk_node_state_t s)
{
	switch (s) {
	case S_NONE:
		return "None";
	case S_EVICT:
		return "Evicted";
	case S_INIT:
		return "Initializing";
	case S_RUN:
		return "Running";
	case S_MASTER:
		return "Master";
	default:
		return "ILLEGAL";
	}
}


void
print_status_block(status_block_t *sb)
{
	if (sb->ps_state == S_NONE)
		return;
	printf("Status block for node %d\n", sb->ps_nodeid);
	printf("\tLast updated by node %d\n", sb->ps_updatenode);
	printf("\tLast updated on %s", ctime((time_t *)&sb->ps_timestamp));
	printf("\tState: %s\n", state_str(sb->ps_state));
	printf("\tFlags: %04x\n", sb->ps_flags);
	printf("\tScore: %d/%d\n", sb->ps_score, sb->ps_scoremax);
	printf("\tAverage Cycle speed: %d.%06d seconds\n", 
		sb->ps_ca_sec, sb->ps_ca_usec);
	printf("\tLast Cycle speed: %d.%06d seconds\n", 
		sb->ps_lc_sec, sb->ps_lc_usec);
	printf("\tIncarnation: %08x%08x\n",
		(int)(sb->ps_incarnation>>32&0xffffffff),
		(int)(sb->ps_incarnation&0xffffffff));

}


void
read_info(char *dev)
{
	target_info_t ti;
	int x;
	status_block_t sb;

	if (qdisk_open(dev, &ti) < 0) {
		printf("Could not read from %s: %s\n",
		       dev, strerror(errno));
		return;
	}

	for (x = 0; x < MAX_NODES_DISK; x++) {

		if (qdisk_read(&ti,
			       qdisk_nodeid_offset(x+1, ti.d_blksz),
			       &sb, sizeof(sb)) < 0) {
			printf("Error reading node ID block %d\n",
			       x+1);
			continue;
		}
		swab_status_block_t(&sb);
		print_status_block(&sb);
	}

	qdisk_close(&ti);
}


void
print_qdisk_info(struct devnode *dn)
{
	quorum_header_t *qh = (quorum_header_t *)dn->filter;
	struct devpath *dp;

	for (dp = dn->devpath; dp; dp = dp->next)
		printf("%s:\n", dp->path);
	printf("\tMagic:                %08x\n", qh->qh_magic);
	printf("\tLabel:                %s\n", qh->qh_cluster);
	printf("\tCreated:              %s",
       		ctime((time_t *)&(qh->qh_timestamp)));
	printf("\tHost:                 %s\n", qh->qh_updatehost);
	printf("\tKernel Sector Size:   %d\n", qh->qh_kernsz);
	if (qh->qh_version == VERSION_MAGIC_V2) {
		printf("\tRecorded Sector Size: %d\n\n", (int)qh->qh_blksz);
	}
}

int
find_partitions(const char *label, char *devname, size_t devlen, int print)
{
	struct devlisthead *dh = NULL;
	struct devnode *dn = NULL;
	struct device_args dargs;

	memset(&dargs, 0, sizeof(dargs));
	dargs.label = (char *)label;
	dargs.flags = 1;	/* strict device check */
	dargs.devnode = NULL;	/* First matching device */

	dh = scan_for_dev(NULL, 5, filter_devs, (void *)(&dargs));
	if (!dh)
		goto not_found;
	if (!dargs.devnode)
		goto not_found;

	if (dargs.count > 0 && print) {
		for (dn = dh->devnode; dn; dn = dn->next) {
			if (dn->filter == NULL) {
				continue;
			}

			print_qdisk_info(dn);
			if (print >= 2) {
				/* Print node stuff */
				read_info(dn->devpath->path);
			}
		}
	}

	if (dargs.count == 1 && label) {
		snprintf(devname, devlen, "%s", dargs.devnode->devpath->path);
	}

	for (dn = dh->devnode; dn; dn = dn->next)
		if (dn->filter)
			free(dn->filter);
        free_dev_list(dh);

	if (print)
		/* No errors if we're just printing stuff */
		return 0;

	if (dargs.count == 1 || !label)
		return 0;

	/* more than one match */
	return 1;

   not_found:
        if (dh) {
		for (dn = dh->devnode; dn; dn = dn->next)
			if (dn->filter)
				free(dn->filter);
		free_dev_list(dh);
	}
	errno = ENOENT;
	return -1;
}
