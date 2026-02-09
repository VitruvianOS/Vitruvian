/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <syscalls.h>

#include "KernelDebug.h"
#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {


static __thread pthread_key_t sOnExitKey;


struct thread_data {
	const char* name;
	thread_id father;

	thread_func func;
	void* data;
};


void* thread_run(void* data)
{
	CALLED();

	thread_data* threadData = (thread_data*)data;
	nexus_thread_spawn spawnInfo = { threadData->name, threadData->father };

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_SPAWN, &spawnInfo);
	if (ret < 0) {
		TRACE("thread_run: NEXUS_THREAD_SPAWN failed\n");
		delete threadData;
		exit_thread(B_ERROR);
		return NULL;
	}

	pthread_detach(pthread_self());

	status_t error = threadData->func(threadData->data);

	delete threadData;

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.return_code = B_OK;
	int err = nexus_io(nexus, NEXUS_THREAD_EXIT, &exchange);
	if (err < 0) {
		pthread_exit((void*)(intptr_t) EINVAL);
		return;
	}

	return NULL;
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

	BKernelPrivate::thread_data* threadData
		= new BKernelPrivate::thread_data();
	if (threadData == NULL)
		return B_NO_MEMORY;

	threadData->name = name;
	threadData->father = find_thread(NULL); 
	threadData->func = func;	
	threadData->data = data;

	pthread_t pThread;
	int32 ret = pthread_create(&pThread, NULL,
		BKernelPrivate::thread_run, threadData);
	if (ret != 0) {
		delete threadData;
		return -errno;
	}

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	thread_id id = nexus_io(nexus, NEXUS_THREAD_WAIT_NEWBORN, NULL);
	if (id < 0)
		return B_BAD_THREAD_ID;
	return id;
}


status_t
kill_thread(thread_id thread)
{
	if (thread < 0)
		return B_BAD_THREAD_ID;

	if (thread == find_thread(NULL))
		return B_NOT_ALLOWED;

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
	status_t err = nexus_io(nexus, NEXUS_THREAD_EXIT, &exchange);
	if (status < 0 || err != B_OK) {
		pthread_exit((void*)(intptr_t) EINVAL);
		return;
	}

	return pthread_exit(NULL);
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
	} else
		status = B_ERROR;

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
		debugger("wait_for_thread: you can't wait for your own team!");
		return B_BAD_TEAM_ID;
	}

	if (id == find_thread(NULL)) {
		debugger("wait_for_thread: do you really want to wait for yourself?");
		return B_BAD_THREAD_ID;
	}

	struct nexus_thread_exchange exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.op = NEXUS_THREAD_WAITFOR;
	exchange.receiver = id;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_OP, &exchange);
	if (ret == B_BAD_THREAD_ID) {
		if (kill(id, 0) < 0)
			return B_BAD_THREAD_ID;

		if (kill(id, SIGCONT) < 0)
			return -errno;

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

	// TODO remove me
	if (kill(id, 0) == 0)
		kill(id, SIGCONT);

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_RESUME, id);
	if (ret == B_BAD_THREAD_ID) {
		if (kill(id, 0) < 0)
			return B_BAD_THREAD_ID;

		if (kill(id, SIGCONT) < 0)
			return -errno;

		return B_OK;
	}

	return ret;
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


}
