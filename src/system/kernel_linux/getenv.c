/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t gMutex;
static bool gInitialized = false;


ssize_t
__getenv_reentrant(const char* name, char* buffer, size_t bufferSize)
{
	if (gInitialized == false) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&gMutex, &attr);
		gInitialized = true;
	}
	pthread_mutex_lock(&gMutex);
	char* ret = getenv(name);
	if (ret == NULL)
		return 0;
	else
		strncpy(buffer, ret, bufferSize);

	free(ret);
	pthread_mutex_unlock(&gMutex);
	return bufferSize;
}
