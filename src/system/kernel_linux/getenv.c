/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <stdlib.h>


ssize_t
__getenv_reentrant(const char* name, char* buffer, size_t bufferSize)
{
	char* ret = getenv(name);
	if (ret == NULL)
		return 0;
	else
		strncpy(buffer, ret, bufferSize);

	free(ret);
	return bufferSize;
}
