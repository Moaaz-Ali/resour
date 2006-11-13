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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

/* Local includes */
#include "xvm.h"
#include "simple_auth.h"
#include "mcast.h"
#include "options.h"

#include <ccs.h>

struct arg_info *find_arg_by_char(char arg);
struct arg_info *find_arg_by_string(char *arg);

extern int _debug;

/**
  Parse args from ccs and assign to the specified args structure.
  (This should only be called from fence_xvmd; not fence_xvm!!!)
  
  @param optstr		Command line option string in getopt(3) format
  @param args		Args structure to fill in.
 */
void
args_get_ccs(char *optstr, fence_xvm_args_t *args)
{
	char buf[256];
	int ccsfd = -1, x, n;
	char *val;
	struct arg_info *arg;
	
	if (args->flags & (F_NOCCS | F_HELP | F_VERSION))
		return;

	ccsfd = ccs_connect();
	if (ccsfd < 0) {
		args->flags |= F_CCSFAIL;
		return;
	}

	for (x = 0; x < strlen(optstr); x++) {
		arg = find_arg_by_char(optstr[x]);
		if (!arg)
			continue;

		if (!arg || (arg->opt != '\xff' && 
			     !strchr(optstr, arg->opt))) {
			continue;
		}

		n = snprintf(buf, sizeof(buf), "/cluster/fence_xvmd/@%s\n",
			     arg->stdin_opt);
		if (n == sizeof(buf)) {
			args->flags |= F_CCSERR;
			return;		
		}

		val = NULL;
		if (ccs_get(ccsfd, buf, &val) != 0) {
			if (val) {
				free(val);
				val = NULL;
			}
			continue;
		}

		if (!val)
			continue;

		if (arg->assign)
			arg->assign(args, arg, val);

		if (val) {
			free(val);
			val = NULL;
		}
	}
	
	ccs_disconnect(ccsfd);
}
