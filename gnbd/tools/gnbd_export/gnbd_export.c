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


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "global.h"
#include "gnbd_endian.h"
#include "gnbd_utils.h"
#include "local_req.h"
#include "device.h"
#include "trans.h"

#include "copyright.cf"

#define TIMEOUT_DEFAULT 60

#define MAN_MSG   "Please see man page for details.\n"

int start_gnbd_clusterd(void)
{
  int ret;

  if( (ret = system("gnbd_clusterd")) < 0){
    printe("system() failed. canot start gnbd_clusterd : %s\n",
           strerror(errno));
    return -1;
  }
  if (ret != 0){
    printe("gnbd_clusterd failed\n");
    return -1;
  }
  return 0;
}

void stop_gnbd_clusterd(void)
{
  int ret;

  if( (ret = system("gnbd_clusterd -k")) < 0){
    printe("system() failed. cannot stop gnbd_clusterd : %s\n",
           strerror(errno));
    return;
  }
  if (ret != 0){
    printe("stopping gnbd_clusterd failed\n");
    return;
  }
}

int servcreate(char *name, char *device, uint32_t timeout, uint8_t readonly){
  info_req_t create_req;
  int fd;
  
  if (timeout && start_gnbd_clusterd())
    return 1;

  strncpy(create_req.name, name, 32);
  create_req.name[31] = 0;
  if (strchr(create_req.name, '/')){
    printe("server name %s is invalid. Names cannot contain a '/'\n",
           create_req.name);
    return 1;
  }
  strncpy(create_req.path, device, 1024);
  create_req.path[1023] = 0;
  create_req.timeout = timeout;
  create_req.flags = (((readonly)? GNBD_FLAGS_READONLY : 0) |
                      ((timeout)? GNBD_FLAGS_UNCACHED : 0));
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return 1;
  if (send_cmd(fd, LOCAL_CREATE_REQ, "create") < 0)
    return 1;
  if (write(fd, &create_req, sizeof(create_req)) != sizeof(create_req)){
    printe("sending create data failed : %s\n", strerror(errno));
    return 1;
  }
  if (recv_reply(fd, "create") < 0)
    return 1;
  printm("created GNBD %s serving file %s\n", name, device);
  close(fd);
  return 0;
}

void invalidate_serv(char *name)
{
  name_req_t invalidate_req;
  int fd;
  
  strncpy(invalidate_req.name, name, 32);
  invalidate_req.name[31] = 0;
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    exit(1);
  if (send_cmd(fd, LOCAL_INVALIDATE_REQ, "invalidate") < 0)
    exit(1);
  if (retry_write(fd, &invalidate_req, sizeof(invalidate_req)) < 0){
    printe("sending invalidate request data failed : %s\n", gstrerror(errno));
    exit(1);
  }
  if (recv_reply(fd, "invalidate") < 0)
    exit(1);
  close(fd);
}

void servremove(char *name)
{
  name_req_t remove_req;
  uint32_t reply;
  int fd;

  strncpy(remove_req.name, name, 32);
  remove_req.name[31] = 0;
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    exit(1);
  if (send_cmd(fd, LOCAL_REMOVE_REQ, "remove") < 0)
    exit(1);
  if (retry_write(fd, &remove_req, sizeof(remove_req)) < 0){
    printe("sending remove req failed: %s\n", gstrerror(errno));
    exit(1);
  }
  if (retry_read(fd, &reply, sizeof(reply)) < 0){
    printe("reading remove reply failed : %s\n", gstrerror(errno));
    exit(1);
  }
  if (reply && reply != LOCAL_RM_CLUSTER_REPLY){
    printe("remove request failed : %s\n", gstrerror(errno));
    exit(1);
  }
  if (reply == LOCAL_RM_CLUSTER_REPLY)
    stop_gnbd_clusterd();
  printm("removed GNBD %s\n", name);
  close(fd);
}

int validate(void)
{
  int fd;
  
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return 1;
  if (send_cmd(fd, LOCAL_VALIDATE_REQ, "validate") < 0)
    return 1;
  if (recv_reply(fd, "validate") < 0)
    return 1;
  printm("removed invalid server processes\n");
  return 0;
}
  

int get_list_info(void **buffer, int *buffer_size, int cmd){
  void *buf;
  uint32_t size;
  int n, total;
  int fd;

  *buffer = NULL;
  *buffer_size = 0;
  
  fd = connect_to_comm_device("gnbd_serv");
  if (fd < 0)
    return -1;
  if (send_cmd(fd, cmd, "list") < 0)
    return -1;
  if (recv_reply(fd, "list") < 0)
    return -1;
  if (read(fd, &size, sizeof(size)) != sizeof(size)){
    printe("receiving list size failed : %s\n", strerror(errno));
    return -1;
  }
  if (size == 0){
    *buffer_size = size;
    close(fd);
    return 0;
  }
  buf = malloc(size);
  if (!buf){
    printe("couldn't allocate memory for list : %s\n", strerror(errno));
    return -1;
  }
  total = 0;
  while(total < size){
    n = read(fd, buf + total, size - total);
    if (n <= 0){
      printe("receiving list failed : %s\n", strerror(errno));
      free(buf);
      return -1;
    }
    total += n;
  }

  *buffer = buf;
  *buffer_size = size;
  close(fd);
  return 0;
}


int removeall(int force){
  int size;
  void *buf;
  info_req_t *info;

  if (get_list_info(&buf, &size, LOCAL_FULL_LIST_REQ) < 0)
    return 1;
  if (size == 0)
    return 0;
  info = (info_req_t *)buf;
  while ((void *)info < buf + size){
    if (force)
      invalidate_serv(info->name);
    servremove(info->name);
    info++;
  }
  free(buf);
  return 0;
}

int gserv_list(void){
  int size;
  void *buf;
  gserv_req_t *info;
 
  if (get_list_info(&buf, &size, LOCAL_GSERV_LIST_REQ) < 0)
    return 1;
  if (verbosity == QUIET){
    if (size)
      free(buf);
    return 0;
  }
  if (size == 0){
    printf("no server processes\n");
    return 0;
  }
  info = (gserv_req_t *)buf;
  printf("  pid       client      device\n");
  printf("--------------------------------\n");
  while((void *)info < buf + size){
    printf("%5d  %15s  %s\n", info->pid, info->node,
           info->name);
    info++;
  }
  free(buf);
  return 0;
}

int list(void){
  int size;
  void *buf;
  info_req_t *info;
  int i = 0;
  
  if (get_list_info(&buf, &size, LOCAL_FULL_LIST_REQ) < 0)
    return 1;
  if (verbosity == QUIET){
    if (size)
      free(buf);
    return 0;
  }
  if (size == 0){
    printf("no exported GNBDs\n");
    return 0;
  }
  info = (info_req_t *)buf;
  while ((void *)info < buf + size){
    i++;
    printf("Server[%d] : %s %s\n"
           "--------------------------\n"
           "      file : %s\n"
           "   sectors : %Lu\n"
           "  readonly : %s\n"
           "    cached : %s\n",
           i, info->name, (info->flags & GNBD_FLAGS_INVALID)? "(invalid)" : "",
           info->path, (long long unsigned int)info->sectors,
           (info->flags & GNBD_FLAGS_READONLY)? "yes" : "no",
           (info->flags & GNBD_FLAGS_UNCACHED)? "no" : "yes");
    if (info->timeout)
      printf("   timeout : %u\n", info->timeout);
    else
      printf("   timeout : no\n");
    printf("\n");
    info++;
  }
  free(buf);
  return 0;
}

int usage(void){
  printf(
"Usage:\n"
"\n"
"gnbd_export [options]\n"
"\n"
"Options:\n"
"  -a               validate the servers, and remove the bad ones\n"
"  -c               enable caching (used with -e)\n"
"  -d <device>      the device to create a GNBD on\n"
"  -e <GNBD>        export the specified GNBD\n"
"  -h               print this help message\n"
"  -l               list the exported GNBDS (default)\n"
"  -L               list the server processes\n"
"  -O               Force unexport. (used with -r and -R)\n"
"  -o               export device readonly (used with -e)\n"
"  -q               quiet mode\n"
"  -R               unexport all GNBDs\n"
"  -r [GNBD | list] unexport the specified GNBD(s)\n"
"  -t <seconds>     set the timeout duration\n"              
"  -v               verbose output (useful with -l)\n"
"  -V               version information\n");
  return 0;
}


#define ACTION_EXPORT     1
#define ACTION_REMOVE     2
#define ACTION_LIST       3
#define ACTION_REMOVE_ALL 4
#define ACTION_GSERV_LIST 5
#define ACTION_VALIDATE   6

char action_to_flag(int action){
  switch(action){
  case ACTION_EXPORT:
    return 'e';
  case ACTION_REMOVE:
    return 'r';
  case ACTION_LIST:
    return 'l';
  case ACTION_REMOVE_ALL:
    return 'R';
  case ACTION_GSERV_LIST:
    return 'L';
  case ACTION_VALIDATE:
    return 'a';
  default:
    printe("invalid action value\n");
    return 0;
  }
}

#define set_action(x) \
do{ \
  if (action){ \
    printe("flags -%c and -%c are not compatible\n", action_to_flag(action), \
           action_to_flag(x)); \
    fprintf(stderr, "Please see man page for details.\n"); \
    return 1; \
  } \
  action = (x); \
} while(0)

int main(int argc, char **argv){
  int c, i;
  int action = 0;
  int cached = 0;
  unsigned int timeout = 0;
  int force = 0;
  int readonly = 0;
  char *device = NULL;
  char *gnbd_name = NULL;

  program_name = "gnbd_export";
  while ((c = getopt(argc, argv, "acd:e:hlLOoqrRt:vV")) != -1){
    switch(c){
    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      return 1;
    case 'a':
      set_action(ACTION_VALIDATE);
      continue;
    case 'c':
      cached = 1;
      continue;
    case 'd':
      device = optarg;
      continue;
    case 'e':
      set_action(ACTION_EXPORT);
      gnbd_name = optarg;
      continue;
    case 'h':
      return usage();
    case 'l':
      set_action(ACTION_LIST);
      continue;
    case 'L':
      set_action(ACTION_GSERV_LIST);
      continue;
    case 'O':
      force = 1;
      continue;
    case 'o':
      readonly = 1;
      continue;
    case 'q':
      verbosity = QUIET;
      continue;
    case 'r':
      set_action(ACTION_REMOVE);
      continue;
    case 'R':
      set_action(ACTION_REMOVE_ALL);
      continue;
    case 't':
      if (sscanf(optarg, "%u", &timeout) != 1 || timeout == 0){
        printe("invalid timeout '%s' with -t\n" MAN_MSG, optarg);
        return 1;
      }
      continue;
    case 'v':
      verbosity = VERBOSE;
      continue;
    case 'V':
      printf("%s %s (built %s %s)\n", argv[0],
             GNBD_RELEASE_NAME, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      return 0;
    default:
      printe("invalid option -- %c\n", c);
      printf("Please use '-h' for usage.\n");
      return 1;
    }
  }
  if (timeout && cached){
    printe("the -t option may not be used with the -c option\n" MAN_MSG);
    return 1;
  }
  if ((cached || timeout || device || readonly) && action != ACTION_EXPORT){
    printe("the -c, -t, and -d flags may only be used with -e\n" MAN_MSG);
    return 1;
  }
  if (force && action != ACTION_REMOVE && action != ACTION_REMOVE_ALL){
    printe("the -O option mhy only be used with -r or -R.\n" MAN_MSG);
    return 1;
  }
  if (action != ACTION_REMOVE && optind != argc){
    printe("extra operand for action: %s\n", argv[optind]);
    fprintf(stderr, "please use '-h' for usage.\n");
    return 1;
  }
  switch(action){
  case ACTION_EXPORT:
    if (!device){
      printe("The -d option must be specified with -e.\n" MAN_MSG);
      return 1;
    }
    if (cached == 0 && timeout == 0)
      timeout = TIMEOUT_DEFAULT;
    return servcreate(gnbd_name, device, (uint32_t)timeout, (uint8_t)readonly);
  case ACTION_REMOVE:
    for (i = optind; i < argc; i++){
      if (force)
        invalidate_serv(argv[i]);
      servremove(argv[i]);
    }
    return 0;
  case ACTION_LIST: case 0:
    return list();
  case ACTION_REMOVE_ALL:
    return removeall(force);
  case ACTION_GSERV_LIST:
    return gserv_list();
  case ACTION_VALIDATE:
    return validate();
  default:
    printe("unrecognized action value\n");
    return 1;
  }
}
