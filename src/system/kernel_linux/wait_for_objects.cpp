/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <syscalls.h>


ssize_t
_kern_wait_for_objects(object_wait_info* infos, int numInfos, uint32 flags,
	bigtime_t timeout)
{
	UNIMPLEMENTED();
	return 0;
}
