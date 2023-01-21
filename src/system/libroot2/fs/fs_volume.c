/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <fs_info.h>
#include <syscalls.h>
#include <sys/mount.h>


dev_t
fs_mount_volume(const char* path, const char* device,
	const char* fs_name, uint32 flags, const char* args)
{
    int mountStatus = mount(path, device, fs_name, flags, args);
    if (mountStatus == 0) {
        dev_t mountedPath = dev_for_path(path);
        return mountedPath;
    }
    return B_ERROR;
}


status_t
fs_unmount_volume(const char* path, uint32 flags)
{
	return umount2(path, flags);
}
