/*
 * Copyright 2019-2021, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef LIBROOT2_AUTO_LOCKER_H
#define LIBROOT2_AUTO_LOCKER_H


#include <shared/AutoLocker.h>

#include <pthread.h>


namespace BKernelPrivate {


class MutexLocking {
public:
	inline bool Lock(pthread_mutex_t* mutex)
	{
		return pthread_mutex_lock(mutex) == 0 ? true : false;
	}

	inline void Unlock(pthread_mutex_t* mutex)
	{
		pthread_mutex_unlock(mutex);
	}
};

typedef AutoLocker<pthread_mutex_t, MutexLocking> MutexLocker;


}


#endif
