/*
 * Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "LinuxVolume.h"

#include <fs_info.h>
#include <syscalls.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

#include <pthread.h>

#include "fs/fs_type_filter.h"
#include "fs/fs_caps_user.h"
#include "fs/utils.h"
#include <MountInfo.h>
#include "KernelDebug.h"
#include "Team.h"

#include "../../kernel/nexus/nexus/nexus.h"
#include "../../kernel/nexus/nexus/node_monitor.h"

#include <blkid/blkid.h>

#define WRITE_FLAG_READONLY 0x1
#define WRITE_DEVICE_NAME   0x2


namespace BKernelPrivate {


static void
resolve_volume_name(const BPrivate::MountEntry& entry, char* out)
{
	const char* mntDir = entry.mount_point.String();
	const char* mntFsname = entry.device_path.String();
	const char* mntType = entry.fs_type.String();

	if (strcmp(mntDir, "/") == 0) {
		strlcpy(out, "Vitruvian", B_FILE_NAME_LENGTH);
		return;
	}

	if (strncmp(mntFsname, "/dev/", 5) == 0) {
		char* label = blkid_get_tag_value(NULL, "LABEL", mntFsname);
		if (label != NULL) {
			if (label[0] != '\0')
				strlcpy(out, label, B_FILE_NAME_LENGTH);
			free(label);
			if (out[0] != '\0')
				return;
		}
	}

	const char* base = strrchr(mntDir, '/');
	if (base && base[1] != '\0') {
		strlcpy(out, base + 1, B_FILE_NAME_LENGTH);
		return;
	}

	if (mntType[0] != '\0') {
		strlcpy(out, mntType, B_FILE_NAME_LENGTH);
		return;
	}

	strlcpy(out, mntDir, B_FILE_NAME_LENGTH);
}


static struct {
	pthread_mutex_t	lock = PTHREAD_MUTEX_INITIALIZER;
	dev_t			device = B_INVALID_DEV;
	fs_info			info;
	uint64_t		gen = (uint64_t)-1;
	bool			valid = false;
} sInfoCache;


status_t
LinuxVolume::FillVolumeInfo(const BPrivate::MountEntry& entry, fs_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	const uint64_t gen = BPrivate::MountInfo::Generation();
	const char* mntDir = entry.mount_point.String();

	pthread_mutex_lock(&sInfoCache.lock);
	if (sInfoCache.valid && sInfoCache.device == entry.dev
			&& sInfoCache.gen == gen) {
		*info = sInfoCache.info;
		pthread_mutex_unlock(&sInfoCache.lock);
		return B_OK;
	}
	pthread_mutex_unlock(&sInfoCache.lock);

	memset(info, 0, sizeof(fs_info));

	struct stat st;
	if (stat(mntDir, &st) < 0)
		return B_ENTRY_NOT_FOUND;
	info->dev = entry.dev;
	info->root = st.st_ino;
	strlcpy(info->device_name, entry.device_path.String(), 128);
	strlcpy(info->fsh_name, entry.fs_type.String(), B_OS_NAME_LENGTH);
	resolve_volume_name(entry, info->volume_name);

	struct statvfs vfs;
	if (statvfs(mntDir, &vfs) != 0)
		return -errno;
	info->block_size = (int32) vfs.f_bsize;
	info->io_size = (int32) (vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
	info->total_blocks = (uint64) vfs.f_blocks;
	info->free_blocks = (uint64) vfs.f_bavail;
	info->total_nodes = (uint64) vfs.f_files;
	info->free_nodes = (uint64) vfs.f_favail;

	const char* fsType = entry.fs_type.String();
	const BPrivate::FsCaps::Entry* caps = BPrivate::FsCaps::by_name(fsType);

	bool isPersistent = false;
	if (caps != NULL && (caps->flags & BPrivate::FsCaps::PERSISTENT))
		isPersistent = true;
	if (entry.device_path.Length() > 0
			&& strncmp(entry.device_path.String(), "/dev/", 5) == 0)
		isPersistent = true;
	if (strcmp(mntDir, "/") == 0)
		isPersistent = true;
	if (isPersistent)
		info->flags |= B_FS_IS_PERSISTENT;

	if ((vfs.f_flag & ST_RDONLY) != 0
			|| (entry.flags & MS_RDONLY) != 0
			|| (caps != NULL && (caps->flags & BPrivate::FsCaps::READONLY)))
		info->flags |= B_FS_IS_READONLY;

	int volFd = open(mntDir, O_PATH | O_DIRECTORY | O_CLOEXEC);
	if (volFd >= 0) {
		struct nexus_query_volume_flags req = { .target_fd = volFd, .flags = 0 };
		int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
		if (nmfd >= 0 && ioctl(nmfd, NEXUS_QUERY_VOLUME_FLAGS, &req) == 0)
			info->flags |= (req.flags & ~B_FS_IS_READONLY);
		close(volFd);
	}

	const char* devName = strrchr(entry.device_path.String(), '/');
	devName = devName ? devName + 1 : entry.device_path.String();
	if (devName[0] != '\0' && is_removable_device(devName))
		info->flags |= B_FS_IS_REMOVABLE;

	pthread_mutex_lock(&sInfoCache.lock);
	sInfoCache.device = info->dev;
	sInfoCache.info = *info;
	sInfoCache.gen = gen;
	sInfoCache.valid = true;
	pthread_mutex_unlock(&sInfoCache.lock);

	return B_OK;
}


status_t
LinuxVolume::FillVolumeInfoForDev(dev_t device, fs_info* info)
{
	if (device == B_INVALID_DEV || info == NULL)
		return B_BAD_VALUE;

	BPrivate::MountEntry entry;
	if (!BPrivate::MountInfo::FindByDev(device, &entry))
		return B_BAD_VALUE;
	if (BPrivate::FsCaps::is_pseudo(entry.fs_type.String()))
		return B_BAD_VALUE;

	return FillVolumeInfo(entry, info);
}


dev_t
LinuxVolume::GetNextVolume(int32* cookie)
{
	if (cookie == NULL || *cookie < 0)
		return B_INVALID_DEV;

	auto snap = BPrivate::MountInfo::Snapshot();
	if (!snap) {
		*cookie = -1;
		return B_INVALID_DEV;
	}

	int32 idx = *cookie;
	const int32 n = (int32) snap->size();
	while (idx < n) {
		const BPrivate::MountEntry& e = (*snap)[idx++];
		if (BPrivate::FsCaps::is_pseudo(e.fs_type.String()))
			continue;
		if (e.is_bind)
			continue;
		*cookie = idx;
		return e.dev;
	}

	*cookie = -1;
	return B_INVALID_DEV;
}


} /* namespace BKernelPrivate */


using namespace BKernelPrivate;


dev_t
next_dev(int32* cookie)
{
	CALLED();

	if (cookie == NULL || *cookie < 0)
		return B_INVALID_DEV;

	return LinuxVolume::GetNextVolume(cookie);
}


status_t
fs_stat_dev(dev_t device, fs_info* info)
{
	CALLED();

	return LinuxVolume::FillVolumeInfoForDev(device, info);
}


dev_t
dev_for_path(const char* path)
{
	if (path == NULL)
		return B_INVALID_DEV;

	struct stat st;
	if (stat(path, &st) < 0)
		return B_INVALID_DEV;

	return st.st_dev;
}


status_t
_kern_read_fs_info(dev_t device, fs_info* info)
{
	return fs_stat_dev(device, info);
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
	UNIMPLEMENTED();
	return B_NOT_SUPPORTED;
}


dev_t
_kern_next_device(int32* cookie)
{
	return next_dev(cookie);
}
