/*
 *  Copyright 2019, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <map>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>

#include "main.h"


static std::map<thread_id, pthread_t> gThreadsMap;
static std::map<thread_id, sem_id> gSemsMap;
static __thread sem_id sThreadBlockSem = -1;
extern sem_id gLoadImageLockSem;

struct data_wrap {
	thread_func func;
	void* data;
	thread_id father;

	pid_t tid;
};


// Static initialization for the main thread
class ThreadPool {
public:
	ThreadPool() {
		InitThread(find_thread(NULL));
	}

	static void InitThread(thread_id tid) {
		// TODO: Add thread_id to the name?
		gThreadsMap.insert(std::make_pair(find_thread(NULL), pthread_self()));
		sThreadBlockSem = create_sem(0, "block_thread_sem");
		gSemsMap.insert(std::make_pair(find_thread(NULL), sThreadBlockSem));
	}

	static void DeinitThread(thread_id tid) {
		gThreadsMap.erase(tid);
		gSemsMap.erase(tid);
		// Maybe we want to trace this
		delete_sem(sThreadBlockSem);
		sThreadBlockSem = -1;
	}
};

static ThreadPool init;


void*
_thread_run(void* data)
{
	data_wrap* threadData = (data_wrap*)data;
	threadData->tid = find_thread(NULL);
	ThreadPool::InitThread(threadData->tid);

	_kern_unblock_thread(threadData->father, B_OK);

	_kern_block_thread(B_TIMEOUT, B_INFINITE_TIMEOUT);
	int ret = threadData->func(threadData->data);

	ThreadPool::DeinitThread(threadData->tid);

	delete data;
	return NULL;
}


thread_id
_kern_spawn_thread(thread_func func, const char* name, int32 priority, void* data)
{
	data_wrap* dataWrap = new data_wrap();
	dataWrap->data = data;
	dataWrap->func = func;
	dataWrap->father = find_thread(NULL);

	pthread_t tid;
	int32 ret = pthread_create(&tid, NULL, _thread_run, dataWrap);
	_kern_block_thread(0, B_INFINITE_TIMEOUT);

	return dataWrap->tid;
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
	//https://gist.github.com/gustavorv86/9e98621b44222114524399a3b4302ddb

	// Blocks if there's already a message
	// Otherwise copy the msg and return

	// 1) Wait the queue to be created or to become empty
	// 2) Write the msg
	UNIMPLEMENTED();
	return B_BAD_THREAD_ID;
}


status_t
_kern_receive_data(thread_id* sender, void* buffer, size_t bufferSize)
{
	// Blocks if the queue is empty

	// 1) Check and create the queue eventually
	// 2) Wait for message

	// struct mq_attr attr = QUEUE_ATTR_INITIALIZER;
	// Create the message queue. The queue reader is NONBLOCK.
	// mqd_t mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, QUEUE_PERMS, &attr);
	UNIMPLEMENTED();
	return B_BAD_THREAD_ID;
}


bool
_kern_has_data(thread_id thread)
{
	// Return true if there's a queue and there's a message
	// False otherwise
	UNIMPLEMENTED();
	return false;
}


status_t
_kern_get_thread_info(thread_id id, thread_info* info)
{
	//UNIMPLEMENTED();
	//printf("get thread info %d\n", id);
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
	UNIMPLEMENTED();
	auto elem = gThreadsMap.find(id);
	if (elem != end(gThreadsMap)
			&& pthread_join(elem->second, (void**)_returnCode) == 0) {
		return B_OK;
	}

	if (gLoadImageLockSem != -1)
		release_sem(gLoadImageLockSem);

	int status;
     do {
            int w = waitpid(id, &status, WUNTRACED | WCONTINUED);
            if (w == -1) {
                perror("waitpid");
				return B_BAD_THREAD_ID;
            }

           if (WIFEXITED(status)) {
                printf("exited, status=%d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("killed by signal %d\n", WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                printf("stopped by signal %d\n", WSTOPSIG(status));
            } else if (WIFCONTINUED(status)) {
                printf("continued\n");
            }
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));

	return B_OK;
}


status_t
_kern_suspend_thread(thread_id id)
{
	debugger("V\\OS doesn't support thread suspension"); 
	return B_BAD_THREAD_ID;
}


status_t
_kern_resume_thread(thread_id id)
{
	UNIMPLEMENTED();
	status_t ret = _kern_unblock_thread(id, B_OK);
	if (ret != B_OK && gLoadImageLockSem != -1) {
		release_sem(gLoadImageLockSem);
		return B_OK;
	}

	return ret;
}


status_t
_kern_block_thread(uint32 flags, bigtime_t timeout)
{
	if (sThreadBlockSem == -1)
		debugger("ThreadBlockSem uninitialized");

	acquire_sem_etc(sThreadBlockSem, 1, B_TIMEOUT, B_INFINITE_TIMEOUT);
	return B_OK;
}


status_t
_kern_unblock_thread(thread_id thread, status_t status)
{
	auto pair = gSemsMap.find(thread);
	if (pair != end(gSemsMap)) {
		release_sem(pair->second);
		return B_OK;
	}

	printf("err unblock\n");

	return B_ERROR;
}


status_t
_kern_unblock_threads(thread_id* threads, uint32 count,
	status_t status)
{
	if (threads == NULL || count == 0)
		return B_BAD_VALUE;

	bool success = true;
	for (uint32 i = 0; i < count; i++) {
		if (_kern_unblock_thread(threads[i], status) != B_OK)
			success = false;
	}

	return success ? B_OK : B_ERROR;
}
