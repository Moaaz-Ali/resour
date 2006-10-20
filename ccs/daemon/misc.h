/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __MISC_H__
#define __MISC_H__

typedef struct open_doc {
  int od_refs;
  xmlDocPtr od_doc;
} open_doc_t;


extern volatile int quorate;
extern int update_required;
extern pthread_mutex_t update_lock;
extern open_doc_t *master_doc;

char *get_cluster_name(xmlDocPtr ldoc);
int get_doc_version(xmlDocPtr ldoc);


#endif /* __MISC_H__ */
