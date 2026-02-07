/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <fs_volume.h>

#include "fs/utils.h"
#include "KernelDebug.h"


dev_t
fs_mount_volume(const char* where, const char* device,
	const char* filesystem, uint32 flags, const char* parameters)
{
	CALLED();

	if (where == NULL || where[0] == '\0')
		return B_BAD_VALUE;

	struct stat st;
	if (stat(where, &st) < 0)
		return B_ENTRY_NOT_FOUND;

	if (BKernelPrivate::is_mount_point(where))
		return B_BUSY;

	const char* fsType = NULL;
	char detectedFs[128] = {0};

	if (filesystem && filesystem[0] != '\0')
		fsType = BKernelPrivate::translate_fs_to_linux(filesystem);

	if (fsType == NULL && device != NULL) {
		if (BKernelPrivate::detect_filesystem(device, detectedFs,
				sizeof(detectedFs))) {
			fsType = detectedFs;
		}
	}

	if (fsType == NULL)
		fsType = "auto";

	unsigned long mountFlags = 0;
	if (flags & B_MOUNT_READ_ONLY)
		mountFlags |= MS_RDONLY;

	if (BKernelPrivate::is_readonly_filesystem(fsType))
		mountFlags |= MS_RDONLY;

	char options[512];
	BKernelPrivate::build_mount_options(fsType, parameters,
		options, sizeof(options));

	const char* mountData = options[0] != '\0' ? options : NULL;
	const char* source = device ? device : "none";

	int ret = mount(source, where, fsType, mountFlags, mountData);
	if (ret < 0) {
		int err = errno;

		if (strcmp(fsType, "ntfs3") == 0) {
			ret = mount(source, where, "ntfs", mountFlags, mountData);
			if (ret < 0)
				ret = mount(source, where, "fuseblk", mountFlags, mountData);
		}

		if (ret < 0 && mountData != NULL)
			ret = mount(source, where, fsType, mountFlags, NULL);

		if (ret < 0)
			return -err;
	}

	if (stat(where, &st) < 0)
		return -errno;

	return st.st_dev;
}


status_t
fs_unmount_volume(const char* path, uint32 flags)
{
	CALLED();

	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;

	int umountFlags = 0;
	if (flags & B_FORCE_UNMOUNT)
		umountFlags |= MNT_FORCE;

	int ret = umount2(path, umountFlags);
	if (ret < 0) {
		int err = errno;

		if (err == EBUSY && (flags & B_FORCE_UNMOUNT)) {
			ret = umount2(path, MNT_DETACH);
			if (ret < 0)
				return -errno;
			return B_OK;
		}

		return -err;
	}

	return B_OK;
}
