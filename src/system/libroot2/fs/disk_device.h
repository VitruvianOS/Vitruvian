/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT_DISK_DEVICE
#define _LIBROOT_DISK_DEVICE

#include <fs_volume.h>

#include <ddm_userland_interface_defs.h>

#include "utils.h"


// TODO dev_for_path
static inline partition_id
make_partition_id(const char* devPath)
{
	struct stat st;
	if (stat(devPath, &st) < 0)
		return -1;

	return (partition_id)st.st_ino;
}

static inline bool
is_removable_device(const char* sysBlockPath)
{
	const char* devName = strrchr(sysBlockPath, '/');
	if (devName)
		devName++;
	else
		devName = sysBlockPath;

	return BKernelPrivate::is_removable_device(devName);
}


static inline bool
is_readonly_device(const char* sysBlockPath)
{
	const char* devName = strrchr(sysBlockPath, '/');
	if (devName)
		devName++;
	else
		devName = sysBlockPath;

	return BKernelPrivate::is_readonly_device(devName);
}


static inline bool
read_sysfs_string(const char* path, char* buffer, size_t size)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return false;

	if (fgets(buffer, size, f) == nullptr) {
		fclose(f);
		return false;
	}
	fclose(f);

	size_t len = strlen(buffer);
	while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r'))
		buffer[--len] = '\0';

	return true;
}


static inline bool
read_sysfs_uint64(const char* path, uint64_t* value)
{
	char buf[64];
	if (!read_sysfs_string(path, buf, sizeof(buf)))
		return false;

	*value = strtoull(buf, nullptr, 10);

	return true;
}


static inline uint32
get_block_size(const char* devPath)
{
	int fd = open(devPath, O_RDONLY);
	if (fd < 0)
		return 512;

	int blockSize = 512;

	if (ioctl(fd, BLKSSZGET, &blockSize) < 0)
		blockSize = 512;

	close(fd);
	return (uint32)blockSize;
}


#endif // _LIBROOT_DISK_DEVICE
