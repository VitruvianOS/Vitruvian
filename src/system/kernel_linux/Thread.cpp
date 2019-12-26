/*
 *  Copyright 2019, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <Locker.h>

#include <map>

#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>

#include "main.h"
#include "KernelDebug.h"


class Thread {
public:
						Thread();
						~Thread();

	static thread_id	Spawn(thread_func func, const char* name,
							int32 priority, void* data);

	static status_t		WaitForThread(thread_id id, status_t* _returnCode);
	static status_t		Resume(thread_id id);

	status_t			Block(uint32 flags, bigtime_t timeout);
	static status_t		Unblock(thread_id thread, status_t status);

	static status_t		SendData(thread_id thread, int32 code,
							const void* buffer, size_t buffer_size);
	status_t			ReceiveData(thread_id* sender, void* buffer, size_t bufferSize);
	static bool 		HasData(thread_id thread);

	// Access to various stuff

private:

	// TODO: atfork reset
	thread_id			fThread;
	sem_id				fThreadBlockSem = -1;
	port_id				fThreadPort;

	pthread_t			fNativeThread;
};


struct data_wrap {
	thread_func func;
	void* data;
	thread_id father;

	pid_t tid;
};


static std::map<thread_id, Thread*> gThreadsMap;
static __thread Thread* gCurrentThread;

extern int gLoadImageFD;
static BLocker fLock;


// Static initialization for the main thread
class ThreadPool {
public:

	static void ReinitAtFork()
	{
		TRACE("Process %d reinit thread after fork", getpid());
		gThreadsMap.clear();
		gCurrentThread = new Thread();
		gLoadImageFD = -1;
	}

	ThreadPool()
	{
		fMainThread = new Thread();
		pthread_atfork(NULL, NULL, &ReinitAtFork);
	}

private:
	Thread* fMainThread;
};

static ThreadPool init;


static void*
_thread_run(void* data)
{
	CALLED();

	fLock.Lock();
	data_wrap* threadData = (data_wrap*)data;
	threadData->tid = find_thread(NULL);
	Thread* thread = new Thread();
	fLock.Unlock();

	Thread::Unblock(threadData->father, B_OK);
	thread->Block(B_TIMEOUT, B_INFINITE_TIMEOUT);
	threadData->func(threadData->data);

	fLock.Lock();
	delete thread;
	fLock.Unlock();

	delete threadData;
	return NULL;
}


Thread::Thread()
{
	fThread = find_thread(NULL);

	fThreadPort = create_port(1, std::to_string(fThread).c_str());
	// TODO: Add thread_id to the name?
	fThreadBlockSem = create_sem(0, "block_thread_sem");
	fNativeThread = pthread_self();

	gCurrentThread = this;
	gThreadsMap.insert(std::make_pair(fThread, this));
}


Thread::~Thread()
{
	// Maybe we want to trace this
	delete_sem(fThreadBlockSem);
	delete_port(fThreadPort);

	gThreadsMap.erase(fThread);
}


thread_id
Thread::Spawn(thread_func func, const char* name, int32 priority,
	void* data)
{
	fLock.Lock();
	data_wrap* dataWrap = new data_wrap();
	dataWrap->data = data;
	dataWrap->func = func;
	dataWrap->father = find_thread(NULL);
	fLock.Unlock();

	pthread_t tid;
	int32 ret = pthread_create(&tid, NULL, _thread_run, dataWrap);

	// TODO: errorcheck
	gCurrentThread->Block(B_TIMEOUT, B_INFINITE_TIMEOUT);

	return dataWrap->tid;
}


status_t
Thread::WaitForThread(thread_id id, status_t* _returnCode)
{
	auto elem = gThreadsMap.find(id);
	if (elem != end(gThreadsMap)
			&& pthread_join(elem->second->fNativeThread,
				(void**)_returnCode) == 0) {
		return B_OK;
	}

	if (gLoadImageFD != -1) {
		uint32 value = 1;
		write(gLoadImageFD, (const void*)&value, sizeof(uint32));
		close(gLoadImageFD);
		gLoadImageFD = -1;

		int status;
		do {
			TRACE("waitpid %d\n", id);
			if (waitpid(id, &status, WUNTRACED | WCONTINUED) == -1)
				return B_BAD_THREAD_ID;

			if (WIFEXITED(status)) {
				TRACE("exited, status=%d\n", WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				TRACE("killed by signal %d\n", WTERMSIG(status));
			} else if (WIFSTOPPED(status)) {
				TRACE("stopped by signal %d\n", WSTOPSIG(status));
			} else if (WIFCONTINUED(status)) {
				TRACE("continued\n");
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));

		// TODO: returncode
		return B_OK;
	}

	return B_BAD_THREAD_ID;
}


status_t
Thread::Resume(thread_id id)
{
	return Unblock(id, B_OK);
}


status_t
Thread::Block(uint32 flags, bigtime_t timeout)
{
	if (fThreadBlockSem == -1)
		debugger("ThreadBlockSem uninitialized");

	acquire_sem_etc(fThreadBlockSem, 1, flags, timeout);
	return B_OK;
}


status_t
Thread::Unblock(thread_id thread, status_t status)
{
	auto pair = gThreadsMap.find(thread);
	if (pair != end(gThreadsMap)) {
		release_sem(pair->second->fThreadBlockSem);
		return B_OK;
	}

	TRACE("error unblocking thread\n");
	return B_ERROR;
}


status_t
Thread::SendData(thread_id thread, int32 code, const void* buffer,
	size_t buffer_size)
{
	// Blocks if there's already a message
	// Otherwise copy the msg and return
	port_id id = find_port(std::to_string(thread).c_str());
	if (id < 0) {
		TRACE("port not found\n");
		return B_BAD_THREAD_ID;
	}
	size_t s = write_port(id, code, buffer, buffer_size);
	if (s != buffer_size)
		return B_ERROR;

	return B_OK;
}


status_t
Thread::ReceiveData(thread_id* sender, void* buffer, size_t bufferSize)
{
	// Blocks if there is no message to read
	int32 code;
	size_t size = read_port(gCurrentThread->fThreadPort,
		&code, buffer, bufferSize);

	// TODO: there must be something not right here,
	// enabling the following check looks like make some
	// hypotetically legit calls to fail.

	//if (size <= 0)
	//	printf("read port error\n");
	//	return B_ERROR;

	return B_OK;
}


bool
Thread::HasData(thread_id thread)
{
	// Return true if there's a queue and there's a message
	// False otherwise
	UNIMPLEMENTED();
	return false;
}


thread_id
_kern_spawn_thread(thread_func func, const char* name, int32 priority, void* data)
{
	CALLED();
	return Thread::Spawn(func, name, priority, data);
}


status_t
_kern_kill_thread(thread_id thread)
{
	UNIMPLEMENTED();
	// pthread_cancel?
	return B_BAD_THREAD_ID;
}


status_t
_kern_rename_thread(thread_id thread, const char* newName)
{
	UNIMPLEMENTED();
	return B_OK;
}


void
_kern_exit_thread(status_t status)
{
	pthread_exit((void*)&status);
}


extern "C"
status_t
_kern_on_exit_thread(void (*callback)(void *), void *data)
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
_kern_send_data(thread_id thread, int32 code,
	const void* buffer, size_t buffer_size)
{
	CALLED();
	return Thread::SendData(thread, code, buffer, buffer_size);
}


status_t
_kern_receive_data(thread_id* sender, void* buffer, size_t bufferSize)
{
	CALLED();
	return gCurrentThread->ReceiveData(sender, buffer, bufferSize);
}


bool
_kern_has_data(thread_id thread)
{
	return Thread::HasData(thread);
}


status_t
_kern_get_thread_info(thread_id id, thread_info* info)
{
	CALLED();

	info->thread = id;
	strncpy (info->name, "Unknown", B_OS_NAME_LENGTH);
	info->name[B_OS_NAME_LENGTH - 1] = '\0';
	info->state = B_THREAD_RUNNING;
	info->priority = 5;
	info->team = getpid();
	return B_OK;
}


status_t
_kern_get_next_thread_info(team_id team, int32* _cookie,
	thread_info* info)
{
	// Use proc
	UNIMPLEMENTED();
	return B_BAD_VALUE;
}


thread_id
find_thread(const char* name)
{
	if (name == NULL)
		return syscall(SYS_gettid);

	// Find on shm the name

	UNIMPLEMENTED();
	return B_NAME_NOT_FOUND;
}


status_t
_kern_set_thread_priority(thread_id id, int32 priority)
{
	UNIMPLEMENTED();
	// Mapping:
	// 0, SCHED_IDLE
	// 1-9, SCHED_OTHER 19, 1
	// 10, SCHED_OTHER 0
	// 11-30, SCHED_OTHER -1,-20
	// 31-120, SCHED_FIFO, 1,99
	return B_BAD_THREAD_ID;
}


status_t
_kern_wait_for_thread(thread_id id, status_t* _returnCode)
{
	CALLED();
	return Thread::WaitForThread(id, _returnCode);
}


status_t
_kern_suspend_thread(thread_id id)
{
	debugger("V\\OS doesn't support thread suspend"); 
	return B_BAD_THREAD_ID;
}


status_t
_kern_resume_thread(thread_id id)
{
	CALLED();
	return Thread::Resume(id);
}


status_t
_kern_block_thread(uint32 flags, bigtime_t timeout)
{
	CALLED();
	return gCurrentThread->Block(flags, timeout);
}


status_t
_kern_unblock_thread(thread_id thread, status_t status)
{
	CALLED();
	return Thread::Unblock(thread, status);
}


status_t
_kern_unblock_threads(thread_id* threads, uint32 count,
	status_t status)
{
	CALLED();

	if (threads == NULL || count == 0)
		return B_BAD_VALUE;

	bool success = true;
	for (uint32 i = 0; i < count; i++) {
		if (_kern_unblock_thread(threads[i], status) != B_OK)
			success = false;
	}

	return success ? B_OK : B_ERROR;
}
