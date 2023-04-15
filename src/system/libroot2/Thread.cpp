/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <Locker.h>

#include <string>
#include <map>

#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>

#include "KernelDebug.h"
#include "Team.h"
#include "Thread.h"

#include "../kernel/nexus/nexus.h"


// TODO remove
extern int gLoadImageFD;


namespace BKernelPrivate {


struct data_wrap {
	thread_func func;
	void* data;
	thread_id father;

	pid_t tid;
};

static __thread Thread* gCurrentThread;
static __thread sem_id gThreadExitSem;

static pthread_mutex_t fLock;
static std::map<thread_id, Thread*> gThreadsMap;
static int gNexus = BKernelPrivate::Team::GetNexusDescriptor();

// Static initialization for the main thread
class ThreadPool {
public:

	ThreadPool()
	{
		_Init();
		pthread_atfork(NULL, NULL, &ReinitAtFork);
	}

	static thread_id
	CreateThread()
	{
		thread_id id = find_thread(NULL);
		Thread* thread = new Thread();

		gThreadsMap.insert(std::make_pair(id, thread));
		return id;
	}

	static Thread* Find(thread_id id)
	{
		auto elem = gThreadsMap.find(id);
		if (elem == end(gThreadsMap))
			return NULL;

		return elem->second;
	}

	static void Lock()
	{
		pthread_mutex_lock(&fLock);
	}

	static void Unlock()
	{
		pthread_mutex_unlock(&fLock);
	}

	static void ReinitAtFork()
	{
		TRACE("Process %d reinit thread after fork\n", getpid());
		gThreadsMap.clear();
		_Init();
	}

private:

	static void _Init()
	{
		pthread_mutex_init(&fLock, NULL);
		gLoadImageFD = -1;
		ThreadPool::CreateThread();
	}

	Thread*			fMainThread;
};

static ThreadPool init;


Thread::Thread()
{
	fThread = find_thread(NULL);
	fThreadBlockSem = create_sem(0, "block_thread_sem");
	fThreadExitSem = create_sem(0, "exit_thread_sem");
	gThreadExitSem = fThreadExitSem;
	gCurrentThread = this;
}


Thread::~Thread()
{
	//assert(gettid() == this->fThreadID);
	delete_sem(fThreadBlockSem);
	// TODO: pthread_detach
}


void*
Thread::thread_run(void* data)
{
	CALLED();
	ThreadPool::Lock();
	if (ioctl(gNexus, NEXUS_THREAD_SPAWN, "thread name") < 0) {
		status_t exitStatus = B_ERROR;
		delete_sem(gThreadExitSem);
		pthread_exit(&exitStatus);
		return NULL;
	}
	data_wrap* threadData = (data_wrap*)data;
	threadData->tid = ThreadPool::CreateThread();
	ThreadPool::Unlock();

	Thread::Unblock(threadData->father, B_OK);
	gCurrentThread->Block(B_TIMEOUT, B_INFINITE_TIMEOUT);

	threadData->func(threadData->data);

	ThreadPool::Lock();
	gThreadsMap.erase(gCurrentThread);
	printf("%d is exiting\n", gettid());
	delete gCurrentThread;
	gCurrentThread = NULL;
	delete threadData;
	ThreadPool::Unlock();

	status_t exitStatus = B_OK;
	if (ioctl(gNexus, NEXUS_THREAD_EXIT, NULL) < 0)
		exitStatus = B_ERROR;

	// TODO exit callback pthread_key
	delete_sem(gThreadExitSem);
	pthread_exit(&exitStatus);
	return NULL;
}


status_t
Thread::WaitForThread(thread_id id, status_t* _returnCode)
{
	ThreadPool::Lock();
		auto elem = gThreadsMap.find(id);
		if (elem == end(gThreadsMap)) {
			ThreadPool::Unlock();
			return B_BAD_THREAD_ID;
		}
		sem_id sem = elem->second->fThreadExitSem;
	ThreadPool::Unlock();

	status_t ret = acquire_sem(sem);

	if (_returnCode != NULL)
		*_returnCode = 0;

	// TODO: move to Team
	if (ret != B_OK && gLoadImageFD != -1) {
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

	return ret;
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
	ThreadPool::Lock();
	auto elem = gThreadsMap.find(thread);
	if (elem == end(gThreadsMap)) {
		ThreadPool::Unlock();
		return B_BAD_THREAD_ID;
	}
	Thread* ret = elem->second;
	sem_id blockSem = -1;
	if (ret != NULL)
		blockSem = ret->fThreadBlockSem;
	ThreadPool::Unlock();

	if (blockSem != -1) {
		release_sem(blockSem);
		return B_OK;
	}

	return B_ERROR;
}


}


extern "C" {


thread_id
spawn_thread(thread_func func, const char* name, int32 priority, void* data)
{
	CALLED();

	BKernelPrivate::data_wrap* dataWrap
		= new BKernelPrivate::data_wrap();
	dataWrap->data = data;
	dataWrap->func = func;
	dataWrap->father = find_thread(NULL);

	pthread_t pThread;
	int32 ret = pthread_create(&pThread, NULL,
		BKernelPrivate::Thread::thread_run, dataWrap);
	if (ret < 0)
		return B_ERROR;

	BKernelPrivate::gCurrentThread->Block(B_TIMEOUT,
		B_INFINITE_TIMEOUT);

	return dataWrap->tid;
}


// Unsafe, Deprecated
status_t
kill_thread(thread_id thread)
{
	UNIMPLEMENTED();
	return B_BAD_THREAD_ID;
}


status_t
rename_thread(thread_id thread, const char* newName)
{
	UNIMPLEMENTED();
	return B_OK;
}


void
exit_thread(status_t status)
{
	// TODO what about the status?
	pthread_exit((void*)&status);
}


status_t
on_exit_thread(void (*callback)(void *), void *data)
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
send_data(thread_id thread, int32 code,
	const void* buffer, size_t bufferSize)
{
	CALLED();

	if (buffer == NULL)
		return B_ERROR;

	struct nexus_thread_exchange exchange;
	exchange.op = NEXUS_THREAD_WRITE;
	exchange.buffer = buffer;
	exchange.size = bufferSize;
	exchange.return_code = code;
	exchange.receiver = thread;

	if (ioctl(BKernelPrivate::gNexus, NEXUS_THREAD_OP, &exchange) < 0)
		return B_ERROR;

	return B_OK;
}


status_t
receive_data(thread_id* sender, void* buffer, size_t bufferSize)
{
	CALLED();

	if (sender == NULL)
		return B_ERROR;

	struct nexus_thread_exchange exchange;
	exchange.op = NEXUS_THREAD_READ;
	exchange.buffer = buffer;
	exchange.size = bufferSize;
	if (ioctl(BKernelPrivate::gNexus, NEXUS_THREAD_OP, &exchange) < 0)
		return B_ERROR;

	*sender = exchange.sender;

	return exchange.return_code;
}


bool
has_data(thread_id thread)
{
	if (thread < 0)
		return false;

	struct nexus_thread_exchange exchange;
	exchange.op = NEXUS_THREAD_HAS_DATA;
	exchange.receiver = thread;

	if (ioctl(BKernelPrivate::gNexus, NEXUS_THREAD_OP, &exchange) < 0)
		return false;

	return exchange.return_code == B_OK;
}


status_t
_get_thread_info(thread_id id, thread_info* info, size_t size)
{
	CALLED();

	if (id < 0 || info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	info->thread = id;
	strncpy(info->name, "Unknown", B_OS_NAME_LENGTH);
	info->name[B_OS_NAME_LENGTH - 1] = '\0';
	info->state = B_THREAD_RUNNING;
	info->priority = 5;
	info->team = getpid();
	return B_OK;
}


status_t
_get_next_thread_info(team_id team, int32* cookie,
	thread_info* info, size_t size)
{
	if (info == NULL || cookie == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	// TODO: Use proc?
	UNIMPLEMENTED();
	return B_BAD_VALUE;
}


thread_id
find_thread(const char* name)
{
	if (name == NULL)
		return syscall(SYS_gettid);

	debugger("You are calling find_thread with something different than NULL");

	// TODO: deprecate

	UNIMPLEMENTED();
	return B_NAME_NOT_FOUND;
}


status_t
set_thread_priority(thread_id id, int32 priority)
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
wait_for_thread(thread_id id, status_t* _returnCode)
{
	CALLED();
	return BKernelPrivate::Thread::WaitForThread(id, _returnCode);
}


status_t
suspend_thread(thread_id id)
{
	debugger("V\\OS doesn't support thread suspend"); 
	return B_BAD_THREAD_ID;
}


status_t
resume_thread(thread_id id)
{
	CALLED();
	return BKernelPrivate::Thread::Resume(id);
}


status_t
snooze(bigtime_t time)
{
	//CALLED();

	return usleep(time);
}


status_t
snooze_until(bigtime_t time, int timeBase)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


status_t
_kern_block_thread(uint32 flags, bigtime_t timeout)
{
	CALLED();
	return BKernelPrivate::gCurrentThread->Block(flags, timeout);
}


status_t
_kern_unblock_thread(thread_id thread, status_t status)
{
	CALLED();
	return BKernelPrivate::Thread::Unblock(thread, status);
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

}
