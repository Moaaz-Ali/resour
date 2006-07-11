/*
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
*/
#include <message.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cman-private.h>

#define MYPORT 190

int my_node_id = 0;
int running = 1;


void
sighandler(int sig)
{
	running = 0;
}


void *
piggyback(void *arg)
{
	msgctx_t ctx;
	char buf[4096];

	if (msg_open(MSG_CLUSTER, 0, MYPORT, &ctx, 0) != 0) {
		printf("Could not set up mcast socket!\n");
		return NULL;
	}

	printf("PIGGYBACK CONTEXT\n");
	msg_print(&ctx);
	printf("END PIGGYBACK CONTEXT\n");
	
	while (running) {
		if (msg_receive(&ctx, buf, sizeof(buf), 2) > 0) {
			printf("Piggyback received: %s\n", buf);
		}
	}

	msg_close(&ctx);

	printf("PIGGY flies...\n");

	return NULL;
}


void *
private(void *arg)
{
	msgctx_t ctx;
	char buf[4096];

	while (running) {
		sleep(3);

		/* use pseudoprivate channel */
		if (msg_open(MSG_CLUSTER, my_node_id, MYPORT, &ctx, 1) != 0) {
			printf("Could not set up virtual-socket!\n");
			return NULL;
		}

		printf("=== pvt thread channel info ===\n");
		msg_print(&ctx);
		printf("=== end pvt thread channel info ===\n");
		fflush(stdout);

		snprintf(buf, sizeof(buf), "Hello!\n");
		msg_send(&ctx, buf, strlen(buf)+1);

		if (msg_receive(&ctx, buf, sizeof(buf), 10) > 0) {
			printf("PRIVATE: Received %s\n", buf);
			fflush(stdout);
		}

		msg_close(&ctx);

		if (msg_open(MSG_CLUSTER, 0, MYPORT, &ctx, 1) != 0) {
			printf("Could not set up mcast socket!\n");
			return NULL;
		}

		snprintf(buf, sizeof(buf), "Babble, babble\n");
		msg_send(&ctx, buf, strlen(buf)+1);
		if (msg_receive(&ctx, buf, sizeof(buf), 1) > 0) {
			printf("PRIVATE: Via MCAST %s\n", buf);
			fflush(stdout);
		}
		msg_close(&ctx);
	}

	printf("Private thread is outta here...\n");

	return NULL;
}


void
clu_initialize(cman_handle_t **ch)
{
	if (!ch)
		exit(1);

	*ch = cman_init(NULL);
	if (!(*ch)) {
		printf("Waiting for CMAN to start\n");

		while (!(*ch = cman_init(NULL))) {
			sleep(1);
		}
	}

        if (!cman_is_quorate(*ch)) {
		/*
		   There are two ways to do this; this happens to be the simpler
		   of the two.  The other method is to join with a NULL group 
		   and log in -- this will cause the plugin to not select any
		   node group (if any exist).
		 */
		printf("Waiting for quorum to form\n");

		while (cman_is_quorate(*ch) == 0) {
			sleep(1);
		}
		printf("Quorum formed, starting\n");
	}
}


int
side_message(msgctx_t *ctx)
{
	msgctx_t actx;
	char buf[1024];

	if (msg_accept(ctx, &actx) < 0)
		return -1;

	printf("=== MAIN: Handling side message ===\n");
	msg_print(&actx);
	fflush(stdout);

	if (msg_receive(&actx, buf, sizeof(buf), 10) > 0) {
		printf("MAIN: Received %s\n", buf);
		snprintf(buf, sizeof(buf), "Goodbye!\n");
		msg_send(&actx, buf, strlen(buf)+1);
	}

	msg_close(&actx);
	
	printf("=== MAIN: end side message ===\n");

	return 0;
}


void
malloc_dump_table(int, int);


void
sigusr2_handler(int sig)
{
}

int
main(int argc, char **argv)
{
	msgctx_t *cluster_ctx;
	char recvbuf[128];
	cman_node_t me;
	int ret;
	pthread_t piggy, priv;
	fd_set rfds;
	int max = 0;
	int port = MYPORT;
	cman_handle_t *clu = NULL;


	clu_initialize(&clu);

	if (cman_init_subsys(clu) < 0) {
		perror("cman_init_subsys");
		return -1;
	}
        cman_get_node(clu, CMAN_NODEID_US, &me);

	my_node_id = me.cn_nodeid;

	if (msg_listen(MSG_CLUSTER, (void *)&port,
	    me.cn_nodeid, &cluster_ctx) < 0) {
		printf("Couldn't set up cluster message system\n");
		return -1;
	}

	signal(SIGTERM, sigusr2_handler);
	signal(SIGUSR2, sigusr2_handler);

	pthread_create(&piggy, NULL, piggyback, NULL);
	pthread_create(&priv, NULL, private, NULL);

	msg_print(cluster_ctx);
	while (running) {
		max = 0;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		msg_fd_set(cluster_ctx, &rfds, &max);

		select(max+1, &rfds, NULL, NULL, NULL);

		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			fgets(recvbuf, 128, stdin);
			if (recvbuf[0] == 'q' || recvbuf[0] == 'Q')
				break;
			if (msg_send(cluster_ctx, recvbuf,
			    strlen(recvbuf)+1) < 0)
				perror("msg_send");
			FD_CLR(STDIN_FILENO, &rfds);
		}
		
		if (!msg_fd_isset(cluster_ctx, &rfds)) 
			continue;

		ret = msg_wait(cluster_ctx, 1);
	
		switch(ret) {
		case M_DATA:
			msg_receive(cluster_ctx, recvbuf, 128, 10);
			printf("MAIN: received %s\n", recvbuf);
			break;
		case M_OPEN:
			printf("MAIN: private connection detected\n");
			side_message(cluster_ctx);
			break;
		case 0:	
			/* No data; probably a control msg */
			break;
		default:
			printf("Cluster EV: %d\n", ret);
			/* Cluster events, etc. */
			msg_receive(cluster_ctx, recvbuf, 128, 0);
		}
	}

	printf("Shutting down...\n");

	running = 0;

	pthread_join(piggy, NULL);
	pthread_join(priv, NULL);

	msg_close(cluster_ctx);
	msg_free_ctx(cluster_ctx);
	msg_shutdown();	

	cman_finish(clu);

	malloc_dump_table(0, 1024);

	exit(0);
}
