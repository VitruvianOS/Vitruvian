/*
 * Copyright 2019, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */

#include <stdlib.h>

#include <syscalls.h>


status_t
_kern_normalize_path(const char* userPath, bool traverseLink, char* buffer)
{
	//UNIMPLEMENTED();
	// TODO: use cwk_path_get_absolute
	buffer = realpath(userPath, NULL);
	printf("normalize path: %s to %s\n", userPath, buffer);
	return B_OK;
}
