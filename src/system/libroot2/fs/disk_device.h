/*
 *  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT_DISK_DEVICE
#define _LIBROOT_DISK_DEVICE

#include <fs_volume.h>
#include <pthread.h>

#include <ddm_userland_interface_defs.h>

#include "utils.h"


static inline partition_id
make_partition_id(const char* devPath, const struct stat* hint = nullptr)
{
	struct stat st;
	if (hint == nullptr) {
		if (stat(devPath, &st) < 0)
			return B_INVALID_DEV;
		hint = &st;
	}

	return S_ISBLK(hint->st_mode) ? (partition_id)hint->st_rdev : (partition_id)hint->st_dev;
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
	static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
	static char sPath[PATH_MAX] = {0};
	static uint32 sValue = 0;

	pthread_mutex_lock(&sLock);
	if (sValue != 0 && strcmp(sPath, devPath) == 0) {
		uint32 hit = sValue;
		pthread_mutex_unlock(&sLock);
		return hit;
	}
	pthread_mutex_unlock(&sLock);

	int fd = open(devPath, O_RDONLY);
	if (fd < 0)
		return 512;

	int blockSize = 512;
	if (ioctl(fd, BLKSSZGET, &blockSize) < 0)
		blockSize = 512;
	close(fd);

	pthread_mutex_lock(&sLock);
	strlcpy(sPath, devPath, sizeof(sPath));
	sValue = (uint32)blockSize;
	pthread_mutex_unlock(&sLock);

	return (uint32)blockSize;
}


#endif // _LIBROOT_DISK_DEVICE
