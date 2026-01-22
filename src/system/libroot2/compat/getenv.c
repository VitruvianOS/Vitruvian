/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <pthread.h>
#include <stdlib.h>


extern char** environ;
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

	int len = strlen(name);
	pthread_mutex_lock(&gMutex);
	for (int i = 0; environ[i] != NULL; i++) {
		if (strncmp(name, environ[i], len) == 0
				&& environ[i][len] == '=') {

			size_t envLen = strlen(&environ[i][len+1]);
			if (envLen >= bufferSize) {
				pthread_mutex_unlock(&gMutex);
				return B_BUFFER_OVERFLOW;
			}

			strcpy(buffer, &environ[i][len+1]);
			pthread_mutex_unlock(&gMutex);
			return B_OK;
		}
	}
	pthread_mutex_unlock(&gMutex);
	return B_ENTRY_NOT_FOUND;
}
