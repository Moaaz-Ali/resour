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

#ifndef __DAEMON_DOT_H__
#define __DAEMON_DOT_H__

int gfs_scand(void *data);
int gfs_glockd(void *data);
int gfs_recoverd(void *data);
int gfs_logd(void *data);
int gfs_quotad(void *data);
int gfs_inoded(void *data);

#endif /* __DAEMON_DOT_H__ */
