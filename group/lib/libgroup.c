/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

#include <sys/types.h>
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>

#include "groupd.h"
#include "libgroup.h"

#define LIBGROUP_MAGIC	0x67727570
#define MAXARGS		100  /* FIXME */

#define VALIDATE_HANDLE(h) \
do { \
	if (!(h) || (h)->magic != LIBGROUP_MAGIC) { \
		errno = EINVAL; \
		return -1; \
	} \
} while (0)

enum {
	DO_STOP = 1,
	DO_START = 2,
	DO_FINISH = 3,
	DO_TERMINATE = 4,
	DO_SET_ID = 5,
	DO_DELIVER = 6,
};


/* if there's more data beyond the number of args we want, the return value
   points to it */

static char *get_args(char *buf, int *argc, char **argv, char sep, int want)
{
	char *p = buf, *rp = NULL;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';

		if (want == i) {
			rp = p + 1;
			break;
		}

		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;

	/* we ended by hitting \0, return the point following that */
	if (!rp)
		rp = strchr(buf, '\0') + 1;

	return rp;
}

void get_nodeids(char *buf, int memb_count, int *nodeids)
{
	char *p;
	int i, count = 0;

	for (i = 0; ; i++) {
		if (isdigit(buf[i]))
			break;
	}

	buf = &buf[i];

	for (i = 0; i < memb_count; i++) {

		nodeids[count++] = atoi(buf);

		p = strchr(buf, ' ');
		if (!p)
			break;

		buf = p + 1;
	}
}

int get_action(char *buf)
{
	char act[16];
	int i;

	memset(act, 0, 16);

	for (i = 0; i < 16; i++) {
		if (isalnum(buf[i]))
			act[i] = buf[i];
		else
			break;
	}

	if (!strncmp(act, "stop", 16))
		return DO_STOP;

	if (!strncmp(act, "start", 16))
		return DO_START;

	if (!strncmp(act, "finish", 16))
		return DO_FINISH;

	if (!strncmp(act, "terminate", 16))
		return DO_TERMINATE;

	if (!strncmp(act, "set_id", 16))
		return DO_SET_ID;

	if (!strncmp(act, "deliver", 16))
		return DO_DELIVER;

	return -1;
}

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 again:
	rv = write(fd, buf + off, count);
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto again;
	}
	return 0;
}

struct group_handle
{
	int magic;
	int fd;
	int level;
	void *private;
	group_callbacks_t cbs;
	char prog_name[32];
};

static int _joinleave(group_handle_t handle, char *name, char *cmd)
{
	char buf[GROUPD_MSGLEN];
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "%s %s", cmd, name);

	return do_write(h->fd, buf, GROUPD_MSGLEN);
}

int group_join(group_handle_t handle, char *name)
{
	return _joinleave(handle, name, "join");
}

int group_leave(group_handle_t handle, char *name)
{
	return _joinleave(handle, name, "leave");
}

int group_stop_done(group_handle_t handle, char *name)
{
	char buf[GROUPD_MSGLEN];
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "stop_done %s", name);

	return do_write(h->fd, buf, GROUPD_MSGLEN);
}

int group_start_done(group_handle_t handle, char *name, int event_nr)
{
	char buf[GROUPD_MSGLEN];
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "start_done %s %d", name, event_nr);

	return do_write(h->fd, buf, GROUPD_MSGLEN);
}

int group_send(group_handle_t handle, char *name, int len, char *data)
{
	char buf[GROUPD_MSGLEN];
	int rv;
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	if (len > 2048 || len <= 0)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	rv = sprintf(buf, "send %s %d", name, len);
	memcpy(buf + rv + 1, data, len);

	return do_write(h->fd, buf, GROUPD_MSGLEN);
}

static int connect_groupd(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

group_handle_t group_init(void *private, char *prog_name, int level,
			  group_callbacks_t *cbs)
{
	struct group_handle *h;
	char buf[GROUPD_MSGLEN];
	int rv, saved_errno;

	h = malloc(sizeof(struct group_handle));
	if (!h)
		return NULL;

	h->magic = LIBGROUP_MAGIC;
	h->private = private;
	h->cbs = *cbs;
	h->level = level;
	strncpy(h->prog_name, prog_name, 32);

	h->fd = connect_groupd();
	if (h->fd < 0)
		goto fail;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "setup %s %d", prog_name, level);

	rv = do_write(h->fd, &buf, GROUPD_MSGLEN);
	if (rv < 0)
		goto fail;

	return (group_handle_t) h;

 fail:
	saved_errno = errno;
	close(h->fd);
	free(h);
	h = NULL;
	errno = saved_errno;
	return NULL;
}

int group_exit(group_handle_t handle)
{
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);
	h->magic = 0;
	close(h->fd);
	free(h);
	return 0;
}

int group_get_fd(group_handle_t handle)
{
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);
	return h->fd;
}

/* Format of string messages we receive from groupd:

   "stop <name>"
   
      name = the name of the group (same for rest)

   "start <name> <event_nr> <type> <memb_count> <memb0> <memb1>..."

      event_nr = used to later match finish/terminate
      type = 1/GROUP_NODE_FAILED, 2/GROUP_NODE_JOIN, 3/GROUP_NODE_LEAVE
      memb_count = the number of group members
      memb0... = the nodeids of the group members

   "finish <name> <event_nr>"
   
      event_nr = matches the start event that's finishing

   "terminate <name> <event_nr>"

      event_nr = matches the start event that's being canceled

   "set_id <name> <id>"

      id = the global id of the group

   "deliver <name> <nodeid> <len>"<data>

      nodeid = who sent the message
      len = length of the message
      data = the message
*/

int group_dispatch(group_handle_t handle)
{
	char buf[GROUPD_MSGLEN], *argv[MAXARGS];
	char *p;
	int act, argc, rv, i, count, *nodeids;
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));

	rv = read(h->fd, &buf, GROUPD_MSGLEN);

	/* FIXME: check rv */

	act = get_action(buf);

	switch (act) {

	case DO_STOP:
		get_args(buf, &argc, argv, ' ', 2);

		h->cbs.stop(h, h->private, argv[1]);
		break;

	case DO_START:
		p = get_args(buf, &argc, argv, ' ', 5);

		count = atoi(argv[4]);
		nodeids = malloc(count * sizeof(int));
		get_nodeids(p, count, nodeids);

		h->cbs.start(h, h->private, argv[1], atoi(argv[2]),
			     atoi(argv[3]), count, nodeids);

		free(nodeids);
		break;

	case DO_FINISH:
		get_args(buf, &argc, argv, ' ', 3);

		h->cbs.finish(h, h->private, argv[1], atoi(argv[2]));
		break;

	case DO_TERMINATE:
		get_args(buf, &argc, argv, ' ', 3);

		/* FIXME: why aren't we passing event_nr, argv[2], through? */

		h->cbs.terminate(h, h->private, argv[1]);
		break;

	case DO_SET_ID:
		get_args(buf, &argc, argv, ' ', 3);

		/* FIXME: id is unsigned, use strtoul() here */

		h->cbs.set_id(h, h->private, argv[1], atoi(argv[2]));
		break;

	case DO_DELIVER:
		p = get_args(buf, &argc, argv, ' ', 4);

		h->cbs.deliver(h, h->private, argv[1], atoi(argv[2]),
			       atoi(argv[3]), p);
		break;
	}

	return 0;
}

int group_get_groups(int max, int *count, group_data_t *data)
{
	char buf[GROUPD_MSGLEN];
	group_data_t empty;
	int fd, rv, maxlen;

	fd = connect_groupd();
	if (fd < 0)
		return fd;

	memset(&empty, 0, sizeof(empty));
	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "get_groups %d", max);

	rv = do_write(fd, &buf, GROUPD_MSGLEN);
	if (rv < 0)
		goto out;

	maxlen = max * sizeof(group_data_t);

	rv = read(fd, data, maxlen);
	if (rv > 0) {
		/* a blank data struct is returned when there are none */
		if (rv == sizeof(empty) && !memcmp(data, &empty, rv))
			*count = 0;
		else
			*count = rv / sizeof(group_data_t);
		rv = 0;
	}
 out:
	close(fd);
	return rv;
}

int group_get_group(int level, char *name, group_data_t *data)
{
	char buf[GROUPD_MSGLEN];
	char data_buf[sizeof(group_data_t)];
	int fd, rv, len;

	fd = connect_groupd();
	if (fd < 0)
		return fd;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "get_group %d %s", level, name);

	rv = do_write(fd, &buf, GROUPD_MSGLEN);
	if (rv < 0)
		goto out;

	rv = read(fd, &data_buf, sizeof(data_buf));

	/* FIXME: check rv */

	memcpy(data, data_buf, sizeof(group_data_t));
	rv = 0;
 out:
	close(fd);
	return rv;
}

