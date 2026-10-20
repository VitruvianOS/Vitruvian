/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ROOT_MEDIA_H
#define _ROOT_MEDIA_H


#include <fs_info.h>
#include <String.h>
#include <Volume.h>
#include <VolumeRoster.h>


// A real writable disk root; live ISO media (squashfs/overlay/ro) never is.
static inline bool
root_is_persistent_rw(BString* fsOut = NULL)
{
	BVolume volume;
	if (BVolumeRoster().GetBootVolume(&volume) != B_OK
		|| volume.IsReadOnly()) {
		return false;
	}

	fs_info info;
	if (fs_stat_dev(volume.Device(), &info) != 0)
		return false;
	if (fsOut != NULL)
		*fsOut = info.fsh_name;

	static const char* kNotPersistent[] = { "", "overlay", "overlayfs",
		"aufs", "squashfs", "erofs", "iso9660", "udf", "tmpfs", "ramfs",
		"rootfs", NULL };
	for (int i = 0; kNotPersistent[i] != NULL; i++) {
		if (strcmp(info.fsh_name, kNotPersistent[i]) == 0)
			return false;
	}
	return true;
}


#endif	// _ROOT_MEDIA_H
