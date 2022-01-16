/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <fs_info.h>

#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdlib.h>
#include <sys/statvfs.h>

#include "KernelDebug.h"

#include "syscalls.h"


//TODO: set errno for public functions


namespace BPrivate {


static FILE* fEntries = NULL;


class LinuxVolume {
public:

	static status_t FillInfo(struct mntent* mountEntry, fs_info* info)
	{
		// TODO: io_size, improve flags
#include <stdlib.h>
		if (strlcpy(info->volume_name, mountEntry->mnt_dir,
				B_FILE_NAME_LENGTH-1) >= B_FILE_NAME_LENGTH) {
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(info->device_name, mountEntry->mnt_fsname,
				127) >= 128) {
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(info->fsh_name, mountEntry->mnt_type,
				B_OS_NAME_LENGTH-1) >= B_OS_NAME_LENGTH) {
			return B_BUFFER_OVERFLOW;
		}

		struct statvfs volume;
		if (statvfs(mountEntry->mnt_dir, &volume) != B_OK)
			return errno;

		info->dev = dev_for_path(mountEntry->mnt_dir);
		info->root = 1;
		info->flags = 0;
		info->block_size = volume.f_bsize;
		info->total_blocks = volume.f_blocks;
		info->free_blocks = volume.f_bavail;
		info->total_nodes = volume.f_files;
		info->free_nodes = volume.f_favail;
		info->io_size = -1;

		if (volume.f_flag & ST_RDONLY)
			info->flags &= B_FS_IS_READONLY;

		return B_OK;
	}

	static struct mntent* FindVolume(dev_t volume)
	{
		struct mntent* mountEntry = NULL;
		FILE* fstab = setmntent("/etc/fstab", "r");
		if (fstab == NULL)
			return NULL;

		while ((mountEntry = getmntent(fstab)) != NULL) {
			dev_t device = dev_for_path(mountEntry->mnt_dir);
			if (volume == device) {
				struct mntent* ret = new struct mntent;
				memcpy(ret, mountEntry, sizeof(struct mntent));
				endmntent(fstab);
				return ret;
			}
		}
		endmntent(fstab);
		return NULL;
	}

	static dev_t GetNext(int32* cookie)
	{
		if (fEntries == NULL)
			fEntries = setmntent("/etc/fstab", "r");

		struct mntent* mountEntry = getmntent(fEntries);
		if (mountEntry == NULL) {
			endmntent(fEntries);
			fEntries = NULL;
			*cookie = -1;
			return B_BAD_VALUE;
		}

		(*cookie)++;
		return dev_for_path(mountEntry->mnt_dir);
	}
};


}


dev_t
next_dev(int32* cookie)
{
	CALLED();

	if (cookie == NULL || *cookie < 0)
		return B_BAD_VALUE;

	return BPrivate::LinuxVolume::GetNext(cookie);
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
fs_stat_dev(dev_t device, fs_info* info)
{
	CALLED();

	if (device <= 0 || info == NULL)
		return B_ERROR;

	struct mntent* entry = BPrivate::LinuxVolume::FindVolume(device);
	if (entry == NULL)
		return B_BAD_VALUE;

	return BPrivate::LinuxVolume::FillInfo(entry, info);
}


dev_t
dev_for_path(const char* path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return -1;

	return st.st_dev;
}
