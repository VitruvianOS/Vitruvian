/*
 ** Copyright 2019, Dario Casalinuovo. All rights reserved
 ** Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include <sys/ioctl.h>

#include "syscalls.h"


status_t
system_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	return _kern_ioctl(fd, cmd, data, length);
}


status_t
_kern_ioctl(int fd, uint32 cmd, void *data, size_t length)
{
	if (ioctl(fd, cmd, data, length) == 0)
		return B_OK;
	else
		return B_ERROR;
}
