/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>


dev_t
_kern_mount(const char* path, const char* device,
	const char* fs_name, uint32 flags, const char* args,
	size_t argsLength)
{
	UNIMPLEMENTED();
	return 0;
}


status_t
_kern_unmount(const char* path, uint32 flags)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
