/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_THREAD
#define _LIBROOT2_THREAD

namespace BKernelPrivate {


class Thread {
public:
						Thread();
						~Thread();

	static status_t		Resume(thread_id id);

	status_t			Block(uint32 flags, bigtime_t timeout);
	static status_t		Unblock(thread_id thread, status_t status);

	static void*		thread_run(void* data);

	static void			ReinitChildAtFork();

	static thread_id	CreateThread();

	static void			Lock();
	static void			Unlock();

private:
	friend class ThreadPool;

	static void			_Init();

	thread_id			fThread;
	sem_id				fThreadBlockSem = -1;
};


}


#endif
