/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>


int	
_kern_open_query(dev_t device, const char* query,
	size_t queryLength, uint32 flags, port_id port,
	int32 token)
{
	UNIMPLEMENTED();
	return 0;
}
