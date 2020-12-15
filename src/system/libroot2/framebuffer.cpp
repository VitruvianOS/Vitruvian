/*
 * Copyright 2018-2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include "syscalls.h"


status_t
_kern_frame_buffer_update(addr_t baseAddress, int32 width,
	int32 height, int32 depth, int32 bytesPerRow)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_safemode_option(const char *parameter,
	char *buffer, size_t *_bufferSize)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
