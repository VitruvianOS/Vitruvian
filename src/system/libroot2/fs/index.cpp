/*
 * Copyright 2020-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <fs_index.h>
#include <TypeConstants.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Team.h"
#include <MountInfo.h>

#include "../../kernel/nexus/nexus/nexus.h"


static status_t
map_index_errno(int err)
{
	switch (err) {
		case ENOSYS:
			return B_NOT_SUPPORTED;
		case EPERM:
		case EACCES:
			return B_PERMISSION_DENIED;
		default:
			return -err;
	}
}


static int
open_volume_root_for_dev(dev_t dev)
{
	BPrivate::MountEntry e;
	if (!BPrivate::MountInfo::FindByDev(dev, &e)) {
		errno = ENODEV;
		return -1;
	}
	int volFd = open(e.mount_point.String(),
		O_PATH | O_DIRECTORY | O_CLOEXEC);
	if (volFd < 0)
		errno = ENODEV;
	return volFd;
}


extern "C" status_t
fs_create_index(dev_t device, const char *name, uint32 type, uint32 flags)
{
	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	int volFd = open_volume_root_for_dev(device);
	if (volFd < 0)
		return B_ENTRY_NOT_FOUND;

	struct nexus_index_create req;
	memset(&req, 0, sizeof(req));
	req.target_fd = volFd;
	req.type      = type;
	req.flags     = flags;
	strlcpy(req.name, name, sizeof(req.name));

	int ret = ioctl(nmfd, NEXUS_INDEX_CREATE, &req);
	close(volFd);

	if (ret < 0)
		return map_index_errno(errno);
	return B_OK;
}


extern "C" status_t
fs_remove_index(dev_t device, const char *name)
{
	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	int volFd = open_volume_root_for_dev(device);
	if (volFd < 0)
		return B_ENTRY_NOT_FOUND;

	struct nexus_index_remove req;
	memset(&req, 0, sizeof(req));
	req.target_fd = volFd;
	strlcpy(req.name, name, sizeof(req.name));

	int ret = ioctl(nmfd, NEXUS_INDEX_REMOVE, &req);
	close(volFd);

	if (ret < 0)
		return map_index_errno(errno);
	return B_OK;
}


extern "C" int
fs_stat_index(dev_t device, const char *name, struct index_info *indexInfo)
{
	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	int volFd = open_volume_root_for_dev(device);
	if (volFd < 0)
		return B_ENTRY_NOT_FOUND;

	struct nexus_index_stat req;
	memset(&req, 0, sizeof(req));
	req.target_fd = volFd;
	strlcpy(req.name, name, sizeof(req.name));

	int ret = ioctl(nmfd, NEXUS_INDEX_STAT, &req);
	close(volFd);

	if (ret < 0)
		return map_index_errno(errno);

	if (indexInfo != NULL) {
		indexInfo->type              = req.type;
		indexInfo->size              = (off_t)req.size;
		indexInfo->modification_time = (time_t)req.modification_time;
		indexInfo->creation_time     = (time_t)req.creation_time;
		indexInfo->uid               = (uid_t)req.uid;
		indexInfo->gid               = (gid_t)req.gid;
	}

	return 0;
}


extern "C" DIR *
fs_open_index_dir(dev_t device)
{
	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0) {
		errno = EINVAL;
		return NULL;
	}

	int volFd = open_volume_root_for_dev(device);
	if (volFd < 0)
		return NULL;

	struct nexus_index_dir_open req;
	req.target_fd = volFd;

	int dirFd = ioctl(nmfd, NEXUS_INDEX_DIR_OPEN, &req);
	close(volFd);

	if (dirFd < 0)
		return NULL;

	DIR *dir = fdopendir(dirFd);
	if (dir == NULL)
		close(dirFd);

	return dir;
}


extern "C" int
fs_close_index_dir(DIR *dir)
{
	if (dir == NULL)
		return B_BAD_VALUE;
	return closedir(dir);
}


extern "C" struct dirent *
fs_read_index_dir(DIR *dir)
{
	if (dir == NULL)
		return NULL;
	return readdir(dir);
}


extern "C" void
fs_rewind_index_dir(DIR *dir)
{
	if (dir != NULL)
		rewinddir(dir);
}
