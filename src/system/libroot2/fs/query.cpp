/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>
#include <sys/types.h>

#include "KernelDebug.h"


int
_kern_open_query(dev_t /*device*/, const char* /*query*/,
	size_t /*queryLength*/, uint32 /*flags*/, port_id /*port*/,
	int32 /*token*/)
{
#ifdef __VOS__
	return B_NOT_SUPPORTED;
#else
	UNIMPLEMENTED();
	return 0;
#endif
}
