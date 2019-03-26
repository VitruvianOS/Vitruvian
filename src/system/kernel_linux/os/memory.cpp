/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>

#include <syscalls.h>


status_t
_kern_get_memory_properties(team_id teamID, const void* address, uint32* _protected,
	 uint32* _lock)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
