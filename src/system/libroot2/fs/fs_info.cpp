/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "LinuxVolume.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/statvfs.h>

#include "KernelDebug.h"

#include "syscalls.h"


//TODO: set errno for public functions


namespace BKernelPrivate {


static FILE* fEntries = NULL;


status_t
LinuxVolume::FillInfo(struct mntent* mountEntry, fs_info* info)
{
		// TODO: io_size, improve flags
		if (strlcpy(info->volume_name, mountEntry->mnt_dir,
				B_FILE_NAME_LENGTH-1) >= B_FILE_NAME_LENGTH-1) {
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(info->device_name, mountEntry->mnt_fsname,
				127) >= 127) {
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(info->fsh_name, mountEntry->mnt_type,
				B_OS_NAME_LENGTH-1) >= B_OS_NAME_LENGTH-1) {
			return B_BUFFER_OVERFLOW;
		}

		struct statvfs volume;
		if (statvfs(mountEntry->mnt_dir, &volume) != B_OK)
			return -errno;

		struct stat st;
		if (stat(mountEntry->mnt_dir, &st) < 0)
			return B_ENTRY_NOT_FOUND;

		info->dev = st.st_dev;
		info->root = st.st_ino;
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


struct mntent*
LinuxVolume::FindVolume(dev_t volume)
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


dev_t
LinuxVolume::GetNext(int32* cookie)
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


}


dev_t
next_dev(int32* cookie)
{
	CALLED();

	if (cookie == NULL || *cookie < 0)
		return B_BAD_VALUE;

	return BKernelPrivate::LinuxVolume::GetNext(cookie);
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

	if (device == B_INVALID_DEV || info == NULL)
		return B_ERROR;

	struct mntent* entry = BKernelPrivate::LinuxVolume::FindVolume(device);
	if (entry == NULL)
		return B_BAD_VALUE;

	return BKernelPrivate::LinuxVolume::FillInfo(entry, info);
}


dev_t
dev_for_path(const char* path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return B_INVALID_DEV;

	return st.st_dev;
}
