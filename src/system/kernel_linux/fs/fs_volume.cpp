/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>
#include <sys/mount.h>


dev_t
_kern_mount(const char* path, const char* device,
	const char* fs_name, uint32 flags, const char* args,
	size_t argsLength)
{
/*    int mount(const char *source, const char *target,
                 const char *filesystemtype, unsigned long mountflags,
                 const void *data);*/
    mount(path, device, fs_name, flags, args);
}


status_t
_kern_unmount(const char* path, uint32 flags)
{
	return umount2(path, flags);
}
