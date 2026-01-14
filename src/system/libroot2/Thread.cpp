/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <Locker.h>

#include <string>
#include <map>

#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>

#include "KernelDebug.h"
#include "Team.h"
#include "Thread.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {


struct data_wrap {
	thread_func func;
	void* data;
	thread_id father;

	pid_t tid;
};

static __thread Thread* gCurrentThread = NULL;
static __thread pthread_key_t sOnExitKey;

static pthread_mutex_t gLock;
static std::map<thread_id, Thread*> gThreadsMap;


// Static initialization for the main thread
class ThreadPool {
public:

	ThreadPool()
	{
		Thread::_Init();
	}

};

static ThreadPool init;


Thread::Thread()
{
	fThread = find_thread(NULL);
	fThreadBlockSem = create_sem(0, "block_thread_sem");
	if (fThreadBlockSem < 0) {
		TRACE("Thread: Failed to create block semaphore: %ld\n", fThreadBlockSem);
	}
	gCurrentThread = this;
}


Thread::~Thread()
{
	if (fThreadBlockSem >= 0) {
		delete_sem(fThreadBlockSem);
		fThreadBlockSem = -1;
	}
	// TODO: pthread_detach
}


void
Thread::ReinitChildAtFork()
{
	TRACE("Process %d reinit thread after fork\n", getpid());

	gThreadsMap.clear();
	gCurrentThread = NULL;

	_Init();
}


thread_id
Thread::CreateThread()
{
	thread_id id = find_thread(NULL);
	Thread* thread = new Thread();

	if (thread->fThreadBlockSem < 0) {
		TRACE("CreateThread: block sem creation failed\n");
		delete thread;
		return B_NO_MEMORY;
	}

	gThreadsMap.insert(std::make_pair(id, thread));
	return id;
}


void
Thread::Lock()
{
	pthread_mutex_lock(&gLock);
}

void
Thread::Unlock()
{
	pthread_mutex_unlock(&gLock);
}


void*
Thread::thread_run(void* data)
{
	CALLED();

	data_wrap* threadData = (data_wrap*)data;

	Thread::Lock();
	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus_io(nexus, NEXUS_THREAD_SPAWN, "thread name") < 0) {
		TRACE("thread_run: NEXUS_THREAD_SPAWN failed\n");
		threadData->tid = -1;
		Thread::Unlock();
		Thread::Unblock(threadData->father, B_ERROR);
		return NULL;
	}

	thread_id tid = CreateThread();
	if (tid < 0) {
		TRACE("thread_run: CreateThread failed\n");
		threadData->tid = -1;
		Thread::Unlock();
		Thread::Unblock(threadData->father, B_ERROR);
		return NULL;
	}

	threadData->tid = tid;
	Thread::Unlock();

	Thread::Unblock(threadData->father, B_OK);

	status_t blockResult = gCurrentThread->Block(B_TIMEOUT, B_INFINITE_TIMEOUT);
	if (blockResult != B_OK) {
		TRACE("thread_run: Block failed with %ld\n", blockResult);
	}

	status_t threadResult = threadData->func(threadData->data);

	Thread::Lock();
	gThreadsMap.erase(threadData->tid);
	TRACE("%d is exiting\n", gettid());
	delete gCurrentThread;
	gCurrentThread = NULL;
	delete threadData;
	Thread::Unlock();

	exit_thread(threadResult);
	return NULL;
}


status_t
Thread::Resume(thread_id id)
{
	return Unblock(id, B_OK);
}


#if 0

status_t
Thread::Block(uint32 flags, bigtime_t timeout)
{
	CALLED();

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_BLOCK;
	exchange.flags = flags;
	exchange.timeout = timeout;

	int sNexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(sNexus, NEXUS_THREAD_OP, &exchange);
	return ret;
}


status_t
Thread::Unblock(thread_id thread, status_t status)
{
	CALLED();

	if (thread < 0)
		return B_BAD_THREAD_ID;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_UNBLOCK;
	exchange.receiver = thread;
	exchange.status = status;

	int sNexus = BKernelPrivate::Team::GetNexusDescriptor();
	return nexus_io(sNexus, NEXUS_THREAD_OP, &exchange);
}


#else

status_t
Thread::Block(uint32 flags, bigtime_t timeout)
{
	if (fThreadBlockSem < 0) {
		TRACE("Block: ThreadBlockSem uninitialized or invalid\n");
		debugger("ThreadBlockSem uninitialized");
		return B_ERROR;
	}

	return acquire_sem_etc(fThreadBlockSem, 1, flags, timeout);
}


status_t
Thread::Unblock(thread_id thread, status_t status)
{
	Thread::Lock();
	auto elem = gThreadsMap.find(thread);
	if (elem == end(gThreadsMap)) {
		Thread::Unlock();
		return B_BAD_THREAD_ID;
	}

	Thread* ret = elem->second;
	sem_id blockSem = -1;
	if (ret != NULL)
		blockSem = ret->fThreadBlockSem;
	Thread::Unlock();

	if (blockSem >= 0) {
		status_t result = release_sem(blockSem);
		if (result != B_OK) {
			TRACE("Unblock: release_sem failed with %ld\n", result);
		}
		return result;
	}

	return B_ERROR;
}

#endif


void
Thread::_Init()
{
	pthread_mutex_init(&gLock, NULL);

	thread_id id = CreateThread();
	if (id < 0) {
		TRACE("Thread::_Init: CreateThread failed\n");
	}
}


}


extern "C" {


thread_id
spawn_thread(thread_func func, const char* name, int32 priority, void* data)
{
	CALLED();

	if (func == NULL) {
		return B_BAD_VALUE;
	}

	BKernelPrivate::data_wrap* dataWrap
		= new BKernelPrivate::data_wrap();
	if (dataWrap == NULL)
		return B_NO_MEMORY;

	dataWrap->data = data;
	dataWrap->func = func;
	dataWrap->father = find_thread(NULL);
	dataWrap->tid = -1;

	pthread_t pThread;
	int32 ret = pthread_create(&pThread, NULL,
		BKernelPrivate::Thread::thread_run, dataWrap);
	if (ret != 0) {
		delete dataWrap;
		return -errno;
	}

	status_t blockResult = BKernelPrivate::gCurrentThread->Block(B_TIMEOUT,
		B_INFINITE_TIMEOUT);

	if (blockResult != B_OK)
		TRACE("spawn_thread: Block failed with %ld\n", blockResult);

	if (dataWrap->tid < 0)
		return B_ERROR;

	return dataWrap->tid;
}


status_t
kill_thread(thread_id thread)
{
	status_t ret = kill_team(thread);
	return ret == B_BAD_TEAM_ID ? B_BAD_THREAD_ID : ret;
}


status_t
rename_thread(thread_id thread, const char* newName)
{
	CALLED();

	if (newName == NULL)
		return B_BAD_VALUE;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_SET_NAME;
	exchange.buffer = newName;
	exchange.receiver = thread;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	return nexus_io(nexus, NEXUS_THREAD_OP, &exchange);
}


void
exit_thread(status_t status)
{
	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.return_code = status;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	nexus_io(nexus, NEXUS_THREAD_EXIT, &exchange);
	pthread_exit((void*)(intptr_t)status);
}


status_t
on_exit_thread(void (*callback)(void *), void* data)
{
	int ret = pthread_key_create(&BKernelPrivate::sOnExitKey, callback);
	if (ret != 0)
		return B_ERROR;

	ret = pthread_setspecific(BKernelPrivate::sOnExitKey, data);
	if (ret != 0)
		return B_ERROR;

	return B_OK;
}


status_t
send_data(thread_id thread, int32 code,
	const void* buffer, size_t bufferSize)
{
	CALLED();

	if (buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_WRITE;
	exchange.buffer = buffer;
	exchange.size = bufferSize;
	exchange.return_code = code;
	exchange.receiver = thread;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	return nexus_io(nexus, NEXUS_THREAD_OP, &exchange);
}


status_t
receive_data(thread_id* sender, void* buffer, size_t bufferSize)
{
	CALLED();

	if (sender == NULL || buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_READ;
	exchange.buffer = buffer;
	exchange.size = bufferSize;

	// TODO B_INTERRUPTED
	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_OP, &exchange);
	if (ret != B_OK)
		return ret;

	if (sender)
		*sender = exchange.sender;

	return ret;
}


bool
has_data(thread_id thread)
{
	if (thread < 0)
		return false;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_HAS_DATA;
	exchange.receiver = thread;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();

	return nexus_io(nexus, NEXUS_THREAD_OP, &exchange) == B_OK;
}


status_t
_get_thread_info(thread_id id, thread_info* info, size_t size)
{
	CALLED();

	if (id < 0)
		return B_BAD_THREAD_ID;

	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	char path[64];
	char buffer[512];

	snprintf(path, sizeof(path), "/proc/%d/comm", id);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return B_BAD_THREAD_ID;

	memset(info, 0, sizeof(thread_info));
	info->thread = id;

	ssize_t len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);

	if (len > 0) {
		buffer[len] = '\0';
		char* newline = strchr(buffer, '\n');
		if (newline != NULL)
			*newline = '\0';
		strncpy(info->name, buffer, B_OS_NAME_LENGTH - 1);
	} else {
		strncpy(info->name, "unknown", B_OS_NAME_LENGTH - 1);
	}

	info->state = B_THREAD_RUNNING;
	info->priority = 10;
	info->team = id;

	snprintf(path, sizeof(path), "/proc/%d/status", id);
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);

		if (len > 0) {
			buffer[len] = '\0';

			char* tgid = strstr(buffer, "Tgid:");
			if (tgid != NULL)
				info->team = atoi(tgid + 5);

			char* state = strstr(buffer, "State:");
			if (state != NULL) {
				state += 6;
				while (*state == ' ' || *state == '\t')
					state++;
				switch (*state) {
					case 'R':
						info->state = B_THREAD_RUNNING;
						break;
					case 'S':
					case 'D':
						info->state = B_THREAD_WAITING;
						break;
					case 'T':
						info->state = B_THREAD_SUSPENDED;
						break;
					default:
						info->state = B_THREAD_RUNNING;
						break;
				}
			}
		}
	}

	return B_OK;
}


status_t
_get_next_thread_info(team_id team, int32* cookie, thread_info* info, size_t size)
{
	if (cookie == NULL || info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	if (team == 0)
		team = getpid();

	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/task", team);

	DIR* dir = opendir(path);
	if (dir == NULL)
		return B_BAD_TEAM_ID;

	struct dirent* entry;
	int32 index = 0;

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		if (index == *cookie) {
			thread_id tid = atoi(entry->d_name);
			closedir(dir);
			(*cookie)++;
			return _get_thread_info(tid, info, size);
		}
		index++;
	}

	closedir(dir);
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
	return B_OK;
}


status_t
wait_for_remote_thread(thread_id id, status_t* returnCode)
{
	CALLED();

	if (id <= 0)
		return B_BAD_VALUE;

	errno = 0;
	int pfd = syscall(SYS_pidfd_open, id, 0);
	if (pfd < 0)
		return pfd;

	struct pollfd p = { .fd = pfd, .events = POLLIN };
	int pollfd = poll(&p, 1, -1);
	if (pollfd < 0) {
		int saved = errno;
		close(pfd);
		if (saved == EINTR)
			return B_INTERRUPTED;
		return B_ERROR;
	}

	siginfo_t si;
	memset(&si, 0, sizeof(si));
	if (waitid(P_PIDFD, pfd, &si, WEXITED | WNOWAIT) != 0) {
		int saved = errno;
		close(pfd);
		if (saved == ECHILD)
			return B_ENTRY_NOT_FOUND;
		return B_ERROR;
	}

	status_t status = B_ERROR;
	if (si.si_code == CLD_EXITED) {
		status = (status_t)si.si_status;
	} else if (si.si_code == CLD_KILLED || si.si_code == CLD_DUMPED) {
		status = (status_t)(-si.si_status);
	} else {
		status = B_ERROR;
	}

	waitid(P_PIDFD, pfd, &si, WEXITED);
	close(pfd);

	if (returnCode)
		*returnCode = status;

	return B_OK;
}


status_t
wait_for_thread(thread_id id, status_t* returnCode)
{
	CALLED();

	if (id < 0)
		return B_BAD_THREAD_ID;

	if (id == getpid()) {
		debugger("Team::WaitForTeam: cannot wait for own team");
		return B_BAD_TEAM_ID;
	}

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_WAITFOR;
	exchange.receiver = id;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_OP, &exchange);

	if (ret == B_BAD_THREAD_ID) {
		if (kill(id, 0) < 0)
			return -errno;

		kill(id, SIGCONT);
		return wait_for_remote_thread(id, returnCode);
	}

	if (ret == B_INTERRUPTED)
		return B_INTERRUPTED;

	if (ret != B_OK)
		return ret;

	if (returnCode != NULL)
		*returnCode = exchange.return_code;

	return B_OK;
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
	return usleep(time);
}


status_t
snooze_until(bigtime_t time, int timeBase)
{
	if (timeBase != B_SYSTEM_TIMEBASE)
		return B_ERROR;

	bigtime_t now = system_time();
	if (time <= now)
		return B_OK;

	return snooze(time - now);
}


status_t
_kern_block_thread(uint32 flags, bigtime_t timeout)
{
	CALLED();

	if (BKernelPrivate::gCurrentThread == NULL) {
		TRACE("_kern_block_thread: no current thread!\n");
		return B_ERROR;
	}

	return BKernelPrivate::gCurrentThread->Block(flags, timeout);
}


status_t
_kern_unblock_thread(thread_id thread, status_t status)
{
	CALLED();
	return BKernelPrivate::Thread::Unblock(thread, status);
}


// TODO this appears to be used only by RWLockManager
// we can remove both.
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
