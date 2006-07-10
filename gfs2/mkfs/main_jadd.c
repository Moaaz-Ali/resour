#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

#include <linux/types.h>
#include <linux/iflags.h>
#include "libgfs2.h"
#include "gfs2_mkfs.h"

#define BUF_SIZE 4096

static char device_name[PATH_MAX];
static char fspath[BUF_SIZE];
static char fsoptions[BUF_SIZE];
static char metafs_path[BUF_SIZE];
static char lock_table[PATH_MAX];
static char meta_mount[PATH_MAX] = "/tmp/.gfs2meta";
static int metafs_fd;
static int metafs_mounted = 0; /* If metafs was already mounted */

void
make_jdata(int fd, char *value)
{
        int err;
        uint32_t val;

        err = ioctl(fd, IFLAGS_GET_IOC, &val);
        if (err)
                die("error doing get flags (%d): %s\n", err, strerror(errno));
        if (strcmp(value, "set") == 0)
                val |= IFLAG_JOURNAL_DATA;
        if (strcmp(value, "clear") == 0)
                val &= ~IFLAG_JOURNAL_DATA;
        err = ioctl(fd, IFLAGS_SET_IOC, &val);
        if (err)
                die("error doing set flags (%d): %s\n", err, strerror(errno));
}

int 
rename2system(struct gfs2_sbd *sdp, char *new_dir, char *new_name)
{
	char oldpath[PATH_MAX], newpath[PATH_MAX];
	int error = 0;
	error = snprintf(oldpath, PATH_MAX, "%s/new_inode", 
			 metafs_path);
	if (error >= PATH_MAX)
		die("rename2system (1)\n");

	error = snprintf(newpath, PATH_MAX, "%s/%s/%s",
			 metafs_path, new_dir, new_name);
	if (error >= PATH_MAX)
		die("rename2system (2)\n");
	
	return rename(oldpath, newpath);
}

void
lock_for_admin(struct gfs2_sbd *sdp)
{
        int error;

        if (sdp->debug)
                printf("\nTrying to get admin lock...\n");

        for (;;) {

                metafs_fd = open(metafs_path, O_RDONLY | O_NOFOLLOW);
                if (metafs_fd < 0)
                        die("can't open %s: %s\n",
                            metafs_path, strerror(errno));

                error = flock(metafs_fd, LOCK_EX);
                if (error)
                        die("can't flock %s: %s\n", metafs_path,
                            strerror(errno));

                break;
        }

        if (sdp->debug)
                printf("Got it.\n");
}

/**
 * print_usage - print out usage information
 *
 */

static void 
print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] /path/to/filesystem\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -c <MB>           Size of quota change file\n");
	printf("  -D                Enable debugging code\n");
	printf("  -h                Print this help, then exit\n");
	printf("  -J <MB>           Size of journals\n");
	printf("  -j <num>          Number of journals\n");
	printf("  -q                Don't print anything\n");
	printf("  -T                Test, do everything except update FS\n");
	printf("  -V                Print program version information, then exit\n");
}

/**
 * decode_arguments - decode command line arguments and fill in the struct gfs2_sbd
 * @argc:
 * @argv:
 * @sdp: the decoded command line arguments
 *
 */

static void
decode_arguments(int argc, char *argv[], struct gfs2_sbd *sdp)
{
	int cont = TRUE;
	int optchar;
	
	while (cont) {
		optchar = getopt(argc, argv, "c:DhJ:j:qTu:VX");
		
		switch (optchar) {
		case 'c':
			sdp->qcsize = atoi(optarg);
			break;
		case 'D':
			sdp->debug = TRUE;
			break;
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;
		case 'J':
			sdp->jsize = atoi(optarg);
			break;
		case 'j':
			sdp->md.journals = atoi(optarg);
			break;
		case 'q':
			sdp->quiet = TRUE;
			break;
		case 'T':
			sdp->test = TRUE;
			break;
		case 'V':
			printf("gfs2_jadd %s (built %s %s)\n", GFS2_RELEASE_NAME,
			       __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;
		case 'X':
			sdp->expert = TRUE;
			break;
		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;
		case EOF:
			cont = FALSE;
			break;
		default:
			die("unknown option: %c\n", optchar);
			break;
		};
	}

	if (optind < argc) {
		sdp->path_name = argv[optind];
		optind++;
	} else
		die("no path specified (try -h for help)\n");
	
	if (optind < argc)
		die("Unrecognized option: %s\n", argv[optind]);

	if (sdp->debug) {
		printf("Command Line Arguments:\n");
		printf("  qcsize = %u\n", sdp->qcsize);
		printf("  jsize = %u\n", sdp->jsize);
		printf("  journals = %u\n", sdp->md.journals);
		printf("  quiet = %d\n", sdp->quiet);
		printf("  test = %d\n", sdp->test);
		printf("  path = %s\n", sdp->path_name);
	}
}

static void 
verify_arguments(struct gfs2_sbd *sdp)
{
	if (!sdp->md.journals)
		die("no journals specified\n");
	if (sdp->jsize < 32 || sdp->jsize > 1024)
		die("bad journal size\n");
	if (!sdp->qcsize || sdp->qcsize > 64)
		die("bad quota change size\n");
}

/**
 * print_results - print out summary information
 * @sdp: the command line
 *
 */

static void 
print_results(struct gfs2_sbd *sdp)
{
	if (sdp->debug)
		printf("\n");
	else if (sdp->quiet)
		return;

	if (sdp->test)
		printf("Test mode:              on\n");
	if (sdp->expert)
		printf("Expert mode:            on\n");

	printf("Filesystem:            %s\n", sdp->path_name);
	printf("Old Journals           %u\n", sdp->orig_journals);
	printf("New Journals           %u\n", sdp->md.journals);

	if (sdp->test)
		printf("\nThe filesystem was not modified.\n");
}

int 
create_new_inode(struct gfs2_sbd *sdp)
{
	char name[PATH_MAX];
	int fd;
	int error;

	error = snprintf(name, PATH_MAX, "%s/new_inode", metafs_path);
	if (error >= PATH_MAX)
		die("create_new_inode (1)\n");

	for (;;) {
		fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd >= 0)
			break;
		if (errno == EEXIST) {
			error = unlink(name);
			if (error)
				die("can't unlink %s: %s\n",
				    name, strerror(errno));
		} else
			die("can't create %s: %s\n", name, strerror(errno));
	}
	
	return fd;
}

void 
add_ir(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		struct gfs2_inum_range ir;
		make_jdata(fd, "set");
		memset(&ir, 0, sizeof(struct gfs2_inum_range));
		do_write(fd, (void*)&ir, sizeof(struct gfs2_inum_range));
	}

	close(fd);
	
	sprintf(new_name, "inum_range%u", sdp->md.journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n", 
		    new_name, error, strerror(errno));
}

void 
add_sc(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;
	
	fd = create_new_inode(sdp);
	
	{
		struct gfs2_statfs_change sc;
		make_jdata(fd, "set");

		memset(&sc, 0, sizeof(struct gfs2_statfs_change));
		do_write(fd, (void*)&sc, sizeof(struct gfs2_statfs_change));
	}

	close(fd);
	
	sprintf(new_name, "statfs_change%u", sdp->md.journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

void 
add_qc(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		char buf[sdp->bsize];
		unsigned int blocks = sdp->qcsize << (20 - sdp->bsize_shift);
		unsigned int x;
		struct gfs2_meta_header mh;

		make_jdata(fd, "clear");
		memset(buf, 0, sdp->bsize);

		for (x=0; x<blocks; x++) {
			do_write(fd, buf, sdp->bsize);
		}

		do_lseek(fd, 0);
		
		memset(&mh, 0, sizeof(struct gfs2_meta_header));
		mh.mh_magic = GFS2_MAGIC;
		mh.mh_type = GFS2_METATYPE_QC;
		mh.mh_format = GFS2_FORMAT_QC;
		gfs2_meta_header_out(&mh, buf);

		for (x=0; x<blocks; x++) {
			do_write(fd, buf, sdp->bsize);
		}

		error = fsync(fd);
		if (error)
			die("can't fsync: %s\n",
			    strerror(errno));
	}

	close(fd);
	
	sprintf(new_name, "quota_change%u", sdp->md.journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

#if 0 /* FIXME: When we have a mountpoint in sysfs for gfs2meta, enable this 
       * to get the lock_table name and block size from the ondisk superblock
       */
void 
read_superblock(struct gfs2_sbd *sdp)
{
	int fd;
	char buf[PATH_MAX];
	
	fd = open(sdp->device_name, O_RDONLY);
	if (fd < 0) {
		die("Could not open the block device %s: %s\n",
			sdp->device_name, strerror(errno));
	}
	do_lseek(fd, 0x10 * 4096);
	do_read(fd, buf, 4096);
	gfs2_sb_in(&(sdp->sd_sb), buf);
	sdp->bsize = sdp->sd_sb.sb_bsize;
	strcpy(lock_table,sdp->sd_sb.sb_locktable);
	sprintf(meta_mount, "%s%s%s", "/sys/fs/gfs2/", lock_table, "/meta");

	close(fd);
}

void 
gather_info(struct gfs2_sbd *sdp)
{
	read_superblock(sdp);
}
#else
void 
gather_info(struct gfs2_sbd *sdp)
{
	struct statfs statbuf;
	if (statfs(sdp->path_name, &statbuf) < 0) {
		die("Could not statfs the filesystem %s: %s\n",
		    sdp->path_name, strerror(errno));
	}
	sdp->bsize = statbuf.f_bsize;
}
#endif 

void 
find_current_journals(struct gfs2_sbd *sdp)
{
	char jindex[PATH_MAX];
	struct dirent *dp;
	DIR *dirp;
	int existing_journals = 0;

	sprintf(jindex, "%s/jindex", metafs_path);
	dirp = opendir(jindex);
	if (!dirp) {
		die("Could not find the jindex directory "
		    "in gfs2meta mount! error: %s\n", strerror(errno));
	}
	while (dirp) {
		if ((dp = readdir(dirp)) != NULL) {
			if (strncmp(dp->d_name, "journal", 7) == 0)
				existing_journals++;
		} else
			goto close;
	}
close:
	closedir(dirp);
	if (existing_journals <= 0) {
		die("There are no journals for this "
		    "gfs2 fs! Did you mkfs.gfs2 correctly?\n");
	}

	sdp->orig_journals = existing_journals;
}

void 
add_j(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		char buf[sdp->bsize];
		unsigned int blocks = sdp->jsize << (20 - sdp->bsize_shift);
		unsigned int x;
		struct gfs2_log_header lh;
		uint64_t seq = RANDOM(blocks);

		make_jdata(fd, "clear");
		memset(buf, 0, sdp->bsize);
		for (x=0; x<blocks; x++) {
			do_write(fd, buf, sdp->bsize);
		}

		do_lseek(fd, 0);

		memset(&lh, 0, sizeof(struct gfs2_log_header));
		lh.lh_header.mh_magic = GFS2_MAGIC;
		lh.lh_header.mh_type = GFS2_METATYPE_LH;
		lh.lh_header.mh_format = GFS2_FORMAT_LH;
		lh.lh_flags = GFS2_LOG_HEAD_UNMOUNT;

		for (x=0; x<blocks; x++) {
			uint32_t hash;

			lh.lh_sequence = seq;
			lh.lh_blkno = x;
			gfs2_log_header_out(&lh, buf);
			hash = gfs2_disk_hash(buf, sizeof(struct gfs2_log_header));
			((struct gfs2_log_header *)buf)->lh_hash = cpu_to_le32(hash);

			do_write(fd, buf, sdp->bsize);

			if (++seq == blocks)
				seq = 0;
		}

		error = fsync(fd);
		if (error)
			die("can't fsync: %s\n",
			    strerror(errno));
	}

	close(fd);
	
	sprintf(new_name, "journal%u", sdp->md.journals);
	error = rename2system(sdp, "jindex", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

static int 
find_gfs2_meta(struct gfs2_sbd *sdp)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char name[] = "gfs2meta";
	char buffer[BUF_SIZE];
	char fstype[80], mfsoptions[BUF_SIZE];
	char meta_device[BUF_SIZE];
	int fsdump, fspass;

	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	while ((fgets(buffer, 4095, fp)) != NULL) {
		buffer[4095] = 0;
		if (strstr(buffer, name) == 0)
			continue;

		if (sscanf(buffer, "%s %s %s %s %d %d", meta_device, 
			   metafs_path, fstype,mfsoptions, &fsdump, 
			   &fspass) != 6)
			continue;
		
		if (strcmp(meta_device, sdp->device_name) != 0)
			continue;
		
		metafs_mounted = 1;
		
		fclose(fp);
		return TRUE;
	}
	fclose(fp);
	return FALSE;
}

static int
dir_exists(const char *dir)
{
	int fd, ret;
	struct stat statbuf;
	fd = open(dir, O_RDONLY);
	if (fd<0) { 
		if (errno == ENOENT)
			return 0;
		die("Couldn't open %s : %s\n", dir, strerror(errno));
	}
	ret = fstat(fd, &statbuf);
	if (ret)
		die("stat failed on %s : %s\n", dir, strerror(errno));
	if (S_ISDIR(statbuf.st_mode)) {
		close(fd);
		return 1;
	}
	close(fd);
	die("%s exists, but is not a directory. Cannot mount metafs here\n", dir);
}

static void 
mount_gfs2_meta(struct gfs2_sbd *sdp) 	 
{
	int ret;
	/* mount the meta fs */
	fprintf(stderr, "mount %s %s %s %d\n", sdp->device_name,
		meta_mount, "gfs2_meta", 0);
	if (!dir_exists(meta_mount)) {
		ret = mkdir(meta_mount, 0700);
		if (ret)
			die("Couldn't create %s : %s\n", meta_mount,
			    strerror(errno));
	}
		
	ret = mount(sdp->device_name, meta_mount, "gfs2meta", 0, NULL);
	if (ret)
		die("Couldn't mount %s : %s\n", meta_mount,
		    strerror(errno));
	strcpy(metafs_path, meta_mount);
}

static void
check_for_gfs2(struct gfs2_sbd *sdp)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char *name = sdp->path_name;
	char buffer[BUF_SIZE];
	char fstype[80];
	int fsdump, fspass, ret;

	if (name[strlen(name) - 1] == '/')
		name[strlen(name) - 1] = '\0';

	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	while ((fgets(buffer, 4095, fp)) != NULL) {
		buffer[4095] = 0;

		if (strstr(buffer, "0") == 0)
			continue;

		if ((ret = sscanf(buffer, "%s %s %s %s %d %d", device_name, fspath, 
				  fstype, fsoptions, &fsdump, &fspass)) != 6) 
			continue;
		sdp->device_name = device_name;

		if (strcmp(fstype, "gfs2") != 0)
			continue;

		if (strcmp(fspath, name) != 0)
			continue;

		fclose(fp);
		if (strncmp(device_name, "/dev/loop", 9) == 0)
			die("Cannot add journal(s) to a loopback GFS mount\n");

		return;
	}
	fclose(fp);
	die("gfs2 Filesystem %s not found\n", name);
}

static void
cleanup(struct gfs2_sbd *sdp)
{
	int ret;
	if (!metafs_mounted) { /* was mounted by us */
		ret = umount(meta_mount);
		if (ret)
			fprintf(stderr, "Couldn't unmount %s : %s\n", meta_mount, 
			    strerror(errno));
	}
}

/**
 * main_jadd - do everything
 * @argc:
 * @argv:
 *
 */

void 
main_jadd(int argc, char *argv[])
{
	struct gfs2_sbd sbd, *sdp = &sbd;
	unsigned int total;

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	sdp->jsize = GFS2_DEFAULT_JSIZE;
	sdp->qcsize = GFS2_DEFAULT_QCSIZE;
	sdp->md.journals = 1;

	decode_arguments(argc, argv, sdp);
	verify_arguments(sdp);
	
	sdp->path_fd = open(sdp->path_name, O_RDONLY);
	if (sdp->path_fd < 0)
		die("can't open root directory %s: %s\n",
		    sdp->path_name, strerror(errno));

	check_for_gfs2(sdp);

	gather_info(sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	compute_constants(sdp);
	find_current_journals(sdp);

	total = sdp->orig_journals + sdp->md.journals;
	for (sdp->md.journals = sdp->orig_journals; 
	     sdp->md.journals < total;
	     sdp->md.journals++) {
		add_ir(sdp);
		add_sc(sdp);
		add_qc(sdp);
		add_j(sdp);
	}

	close(metafs_fd);
	close(sdp->path_fd);

	cleanup(sdp);

	sync();

	print_results(sdp);
}
