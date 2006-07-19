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

#ifndef __LOCK_DLM_DOT_H__
#define __LOCK_DLM_DOT_H__

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <linux/netlink.h>

#include "list.h"
#include "linux_endian.h"
#include "libgroup.h"

#define MAXARGS			16
#define MAXLINE			256
#define MAXNAME			255
#define MAX_CLIENTS		8
#define MAX_MSGLEN		2048
#define MAX_OPTIONS_LEN		1024
#define DUMP_SIZE		(1024 * 1024)

#define LOCK_DLM_GROUP_LEVEL	2
#define LOCK_DLM_GROUP_NAME	"gfs"
#define LOCK_DLM_SOCK_PATH	"gfs_controld_sock"

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

enum {
	DO_STOP = 1,
	DO_START,
	DO_FINISH,
	DO_TERMINATE,
	DO_SETID,
	DO_DELIVER,
};

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[256];
extern char dump_buf[DUMP_SIZE];
extern int dump_point;
extern int dump_wrap;

extern void daemon_dump_save(void);

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)

#define ASSERT(x) \
{ \
	if (!(x)) { \
		fprintf(stderr, "\nAssertion failed on line %d of file %s\n\n" \
			"Assertion:  \"%s\"\n", \
			__LINE__, __FILE__, #x); \
        } \
}

struct mountgroup {
	struct list_head	list;
	uint32_t		id;
	struct list_head	members;
	struct list_head	members_gone;
	int			memb_count;
	struct list_head	resources; /* for plocks */

	char			name[MAXNAME+1];
	char			table[MAXNAME+1];
	char			type[5];
	char			dir[PATH_MAX+1];
	char			options[MAX_OPTIONS_LEN+1];

	int			last_stop;
	int			last_start;
	int			last_finish;
	int			last_callback;
	int			start_event_nr;
	int			start_type;

	char			error_msg[128];
	int			mount_client;
	int			remount_client;
	int			init;
	int			got_our_options;
	int			got_our_journals;
	int			mount_client_notified;
	int			mount_client_delay;
	int			delay_send_journals;
	int			got_kernel_mount;
	int			first_mounter;
	int			first_mounter_done;
	int			emulate_first_mounter;
	int			wait_first_done;
	int			low_finished_nodeid;

	int			needs_recovery;
	int			our_jid;
	int			spectator;
	int			readonly;
	int			rw;
	int			withdraw;

	struct list_head	saved_messages;
	void			*start2_fn;
};

/* mg_member opts bit field */

enum {
	MEMB_OPT_RW		= 1,
	MEMB_OPT_RO		= 2,
	MEMB_OPT_SPECT		= 4,
	MEMB_OPT_RECOVER	= 8,
};

/* these need to match the kernel defines of the same name in
   linux/fs/gfs2/lm_interface.h */

#define LM_RD_GAVEUP 308
#define LM_RD_SUCCESS 309

/* mg_member state: local_recovery_status, recovery_status */

enum {
	RS_NEED_RECOVERY = 1,
	RS_SUCCESS,
	RS_GAVEUP,
	RS_NOFS,
	RS_READONLY,
};

struct mg_member {
	struct list_head	list;
	int			nodeid;
	int			jid;

	int			spectator;
	int			readonly;
	int			rw;
	uint32_t		opts;

	int			tell_gfs_to_recover;
	int			wait_gfs_recover_done;
	int			gone_event;
	int			gone_type;
	int			finished;
	int			local_recovery_status;
	int			recovery_status;
	int			withdrawing;
	int			needs_journals;
};

enum {
	MSG_JOURNAL = 1,
	MSG_OPTIONS,
	MSG_REMOUNT,
	MSG_PLOCK,
	MSG_WITHDRAW,
	MSG_RECOVERY_STATUS,
	MSG_RECOVERY_DONE,
};

#define GDLM_VER_MAJOR 1
#define GDLM_VER_MINOR 0
#define GDLM_VER_PATCH 0

struct gdlm_header {
	uint16_t		version[3];
	uint16_t		type;			/* MSG_ */
	uint32_t		nodeid;			/* sender */
	uint32_t		to_nodeid;		/* 0 if to all */
	char			name[MAXNAME];
};


struct mountgroup *find_mg(char *name);
struct mountgroup *find_mg_id(uint32_t id);

int setup_cman(void);
int process_cman(void);
int setup_cpg(void);
int process_cpg(void);
int setup_groupd(void);
int process_groupd(void);
int setup_plocks(void);
int process_plocks(void);
void exit_cman(void);

int do_mount(int ci, char *dir, char *type, char *proto, char *table,
	     char *options);
int do_unmount(int ci, char *dir);
int do_remount(int ci, char *dir, char *mode);
int do_withdraw(char *name);
int kernel_recovery_done(char *name);
void ping_kernel_mount(char *table);

int client_send(int ci, char *buf, int len);

int send_group_message(struct mountgroup *mg, int len, char *buf);

#endif
