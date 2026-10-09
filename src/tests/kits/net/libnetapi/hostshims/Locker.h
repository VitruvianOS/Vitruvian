/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_LOCKER_H
#define _HOSTSHIMS_LOCKER_H

#include <SupportDefs.h>
#include <pthread.h>

class BLocker {
public:
	BLocker() { pthread_mutex_init(&fMutex, NULL); }
	~BLocker() { pthread_mutex_destroy(&fMutex); }

	bool Lock() { return pthread_mutex_lock(&fMutex) == 0; }
	void Unlock() { pthread_mutex_unlock(&fMutex); }

private:
	pthread_mutex_t fMutex;
};

#endif
