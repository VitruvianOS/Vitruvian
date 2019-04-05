/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de. All rights reserved.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <errno.h>
#include <sys/stat.h>

#include <compat/sys/stat.h>

#include <errno_private.h>
#include <syscalls.h>
//#include <symbol_versioning.h>
#include <syscall_utils.h>


void
convert_to_stat_beos(const struct stat* stat, struct stat_beos* beosStat)
{
	if (stat == NULL || beosStat == NULL)
		return;

	beosStat->st_dev = stat->st_dev;
	beosStat->st_ino = stat->st_ino;
	beosStat->st_mode = stat->st_mode;
	beosStat->st_nlink = stat->st_nlink;
	beosStat->st_uid = stat->st_uid;
	beosStat->st_gid = stat->st_gid;
	beosStat->st_size = stat->st_size;
	beosStat->st_rdev = stat->st_rdev;
	beosStat->st_blksize = stat->st_blksize;
	beosStat->st_atime = stat->st_atime;
	beosStat->st_mtime = stat->st_mtime;
	beosStat->st_ctime = stat->st_ctime;
	#ifdef __HAIKU__
	beosStat->st_crtime = stat->st_crtime;
	#endif
}


void
convert_from_stat_beos(const struct stat_beos* beosStat, struct stat* stat)
{
	if (stat == NULL || beosStat == NULL)
		return;

	stat->st_dev = beosStat->st_dev;
	stat->st_ino = beosStat->st_ino;
	stat->st_mode = beosStat->st_mode;
	stat->st_nlink = beosStat->st_nlink;
	stat->st_uid = beosStat->st_uid;
	stat->st_gid = beosStat->st_gid;
	stat->st_size = beosStat->st_size;
	stat->st_rdev = beosStat->st_rdev;
	stat->st_blksize = beosStat->st_blksize;
	stat->st_atim.tv_sec = beosStat->st_atime;
	stat->st_atim.tv_nsec = 0;
	stat->st_mtim.tv_sec = beosStat->st_mtime;
	stat->st_mtim.tv_nsec = 0;
	stat->st_ctim.tv_sec = beosStat->st_ctime;
	stat->st_ctim.tv_nsec = 0;
	#ifdef __HAIKU__
	stat->st_crtim.tv_sec = beosStat->st_crtime;
	stat->st_crtim.tv_nsec = 0;
	stat->st_type = 0;
	#endif
	stat->st_blocks = 0;
}
