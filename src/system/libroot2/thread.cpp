/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <new>

#include <syscalls.h>

#include "KernelDebug.h"
#include "Team.h"
#include "proc_read.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {


static __thread pthread_key_t sOnExitKey;


struct thread_data {
	char name[B_OS_NAME_LENGTH];

	thread_func func;
	void* data;

	// Creator handshake: the child publishes its tid, the creator registers
	// it with nexus before spawn_thread() returns, then releases the child.
	pthread_mutex_t lock;
	pthread_cond_t cond;
	thread_id tid;
	status_t registered;	// 0 pending, 1 registered, < 0 failed
};


// comm holds 15 characters against B_OS_NAME_LENGTH's 32, and PR_SET_NAME
// fails with ERANGE without setting anything, so truncate by hand.
static void
SetOsThreadName(const char* name)
{
	if (name == NULL)
		return;

	char buffer[16];
	strlcpy(buffer, name, sizeof(buffer));
	prctl(PR_SET_NAME, buffer, 0, 0, 0);
}


void* thread_run(void* data)
{
	CALLED();

	thread_data* threadData = (thread_data*)data;

	pthread_mutex_lock(&threadData->lock);
	threadData->tid = find_thread(NULL);
	pthread_cond_signal(&threadData->cond);
	while (threadData->registered == 0)
		pthread_cond_wait(&threadData->cond, &threadData->lock);
	status_t registered = threadData->registered;
	pthread_mutex_unlock(&threadData->lock);

	if (registered < 0) {
		delete threadData;
		pthread_detach(pthread_self());
		return NULL;
	}

	// Already registered by the creator: this only parks us until
	// resume_thread().
	nexus_thread_spawn spawnInfo = { threadData->name };

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_SPAWN, &spawnInfo);
	if (ret < 0) {
		TRACE("thread_run: NEXUS_THREAD_SPAWN failed\n");
		delete threadData;
		return EINVAL;
	}

	// nexus can't read the name back, so _get_thread_info() takes it from
	// /proc/<tid>/comm.
	SetOsThreadName(threadData->name);

	pthread_detach(pthread_self());

	status_t error = threadData->func(threadData->data);

	delete threadData;

	nexus_io(nexus, NEXUS_THREAD_SET_RETURN_CODE, (void*)(intptr_t)error);
	return NULL;
}


}


namespace {
// 0 means "not cached yet"; a real tid is never 0.
__thread thread_id sCachedTid = 0;


// utime/stime are /proc/<id>/stat fields 14/15; proc_parse_stat finds them
// after the last ')', because a thread name can contain parentheses.
void
GetThreadCpuTimes(int dirFd, const char* prefix, bigtime_t* userTime,
	bigtime_t* kernelTime)
{
	*userTime = 0;
	*kernelTime = 0;

	char name[48];
	snprintf(name, sizeof(name), "%sstat", prefix);
	int fd = proc_openat_file(dirFd, name);
	if (fd < 0)
		return;

	char buffer[1024];
	ssize_t dataRead = proc_read_all(fd, buffer, sizeof(buffer));
	close(fd);
	if (dataRead <= 0)
		return;

	struct proc_stat_info stat;
	if (!proc_parse_stat(buffer, &stat))
		return;

	long hz = proc_clk_tck();
	*userTime = (bigtime_t)((stat.utimeTicks * 1000000ULL) / (uint64)hz);
	*kernelTime = (bigtime_t)((stat.stimeTicks * 1000000ULL) / (uint64)hz);
}


// Inverts the mapping set_thread_priority() applies. Anything unreadable or
// outside the known ranges falls back to 10 rather than guess.
int32
GetThreadPriority(thread_id id)
{
	int policy = sched_getscheduler((pid_t)id);
	if (policy < 0)
		return 10;

	if (policy == SCHED_IDLE)
		return 0;

	if (policy == SCHED_RR || policy == SCHED_FIFO) {
		struct sched_param param;
		memset(&param, 0, sizeof(param));
		if (sched_getparam((pid_t)id, &param) == 0
			&& param.sched_priority >= 1 && param.sched_priority <= 90) {
			// inverse of: sched_priority = 1 + (priority - 31)
			return 31 + (param.sched_priority - 1);
		}
		return 10;
	}

	errno = 0;
	int nice = getpriority(PRIO_PROCESS, (id_t)id);
	if (nice == -1 && errno != 0)
		return 10;

	if (nice == 0)
		return 10;
	if (nice > 0 && nice <= 19)
		// inverse of: nice = 19 - (priority - 1), priority in [1, 9]
		return 20 - nice;
	if (nice < 0 && nice >= -20)
		// inverse of: nice = -1 - (priority - 11), priority in [11, 30]
		return 10 - nice;

	return 10;
}

}


namespace BKernelPrivate {


void
ResetCachedThreadId()
{
	sCachedTid = 0;
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

	if (name != NULL)
		strlcpy(threadData->name, name, sizeof(threadData->name));
	else
		threadData->name[0] = '\0';
	threadData->func = func;
	threadData->data = data;
	pthread_mutex_init(&threadData->lock, NULL);
	pthread_cond_init(&threadData->cond, NULL);
	threadData->tid = 0;
	threadData->registered = 0;

	pthread_t pThread;
	int32 ret = pthread_create(&pThread, NULL,
		BKernelPrivate::thread_run, threadData);
	if (ret != 0) {
		delete threadData;
		if (ret == EAGAIN)
			return B_NO_MORE_THREADS;
		return ret == ENOMEM ? B_NO_MEMORY : B_ERROR;
	}

	// From here the child owns threadData; it only frees it after we
	// release it below, and we don't touch it after that.
	pthread_mutex_lock(&threadData->lock);
	while (threadData->tid == 0)
		pthread_cond_wait(&threadData->cond, &threadData->lock);
	thread_id tid = threadData->tid;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	thread_id id = nexus_io(nexus, NEXUS_THREAD_REGISTER,
		(void*)(intptr_t)tid);

	threadData->registered = id >= 0 ? 1 : id;
	pthread_cond_signal(&threadData->cond);
	pthread_mutex_unlock(&threadData->lock);

	if (id < 0)
		return B_BAD_THREAD_ID;
	return tid;
}


// Linux has no way to kill one thread: SIGKILL to a tid takes down its whole
// thread group, so this kills the thread's team. Documented in OS.h.
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

	if (thread != find_thread(NULL))
		return B_NOT_ALLOWED;

	struct nexus_thread_set_name_req exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.name = newName;
	exchange.size = strlen(newName) + 1;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	if (nexus_io(nexus, NEXUS_THREAD_SET_NAME, &exchange) < 0)
		return B_ERROR;

	BKernelPrivate::SetOsThreadName(newName);
	return B_OK;
}


void
exit_thread(status_t status)
{
	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	nexus_io(nexus, NEXUS_THREAD_SET_RETURN_CODE, (void*)(intptr_t)status);
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

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();

	// B_INTERRUPTED comes before anything was sent; only a kill ends
	// Haiku's send_data().
	struct nexus_thread_rw exchange;
	do {
		memset(&exchange, 0, sizeof(exchange));
		exchange.buffer = const_cast<void*>(buffer);
		exchange.size = bufferSize;
		exchange.return_code = code;
		exchange.receiver = thread;
		if (nexus_io(nexus, NEXUS_THREAD_WRITE, &exchange) != 0)
			return B_BAD_VALUE;
	} while (exchange.ret == B_INTERRUPTED);
	return exchange.ret;
}


status_t
receive_data(thread_id* sender, void* buffer, size_t bufferSize)
{
	CALLED();

	if (sender == NULL || buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();

	// B_INTERRUPTED comes before any data arrived; only a kill ends
	// Haiku's receive_data().
	struct nexus_thread_rw exchange;
	do {
		memset(&exchange, 0, sizeof(exchange));
		exchange.buffer = buffer;
		exchange.size = bufferSize;
		if (nexus_io(nexus, NEXUS_THREAD_READ, &exchange) != 0)
			return B_BAD_VALUE;
	} while (exchange.ret == B_INTERRUPTED);
	if (exchange.ret != B_OK)
		return exchange.ret;

	if (sender)
		*sender = exchange.sender;

	return exchange.return_code;
}


bool
has_data(thread_id thread)
{
	if (thread < 0)
		return false;

	struct nexus_thread_rw exchange;
	memset(&exchange, 0, sizeof(exchange));
	exchange.receiver = thread;

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();

	if (nexus_io(nexus, NEXUS_THREAD_HAS_DATA, &exchange) != 0)
		return false;
	return exchange.ret == B_OK;
}


static status_t
_GetThreadInfoVia(int dirFd, const char* prefix, thread_id id,
	thread_info* info, size_t size)
{
	memset(info, 0, sizeof(thread_info));
	info->thread = id;
	info->team = -1;
	info->state = B_THREAD_RUNNING;

	char buffer[2048];
	char name[48];

	snprintf(name, sizeof(name), "%scomm", prefix);
	int fd = proc_openat_file(dirFd, name);
	if (fd >= 0) {
		ssize_t length = proc_read_all(fd, buffer, sizeof(buffer));
		close(fd);
		if (length > 0) {
			char* newline = strchr(buffer, '\n');
			if (newline != NULL)
				*newline = '\0';
			strlcpy(info->name, buffer, sizeof(info->name));
		} else
			strlcpy(info->name, "unknown", sizeof(info->name));
	} else
		strlcpy(info->name, "unknown", sizeof(info->name));

	// Line-wise parse: a comm value containing "Tgid:" or "State:" must not
	// be mistaken for the field.
	snprintf(name, sizeof(name), "%sstatus", prefix);
	fd = proc_openat_file(dirFd, name);
	if (fd < 0)
		return B_BAD_THREAD_ID;

	ssize_t length = proc_read_all(fd, buffer, sizeof(buffer));
	close(fd);
	struct proc_status_info status;
	if (length <= 0 || !proc_parse_status(buffer, &status) || status.tgid <= 0)
		return B_ERROR;

	info->team = (team_id)status.tgid;
	switch (status.state) {
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

	info->priority = GetThreadPriority(id);

	GetThreadCpuTimes(dirFd, prefix, &info->user_time, &info->kernel_time);

	return B_OK;
}


status_t
_get_thread_info(thread_id id, thread_info* info, size_t size)
{
	CALLED();

	if (id < 0)
		return B_BAD_THREAD_ID;

	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	struct proc_dir dir;
	if (!proc_dir_open_pid(&dir, (pid_t)id))
		return B_BAD_THREAD_ID;

	status_t result = _GetThreadInfoVia(dir.dirFd, "", id, info, size);
	proc_dir_close(&dir);
	return result;
}


namespace {


struct ThreadEnumState {
	ProcDirBatch	batch;
};


} // namespace


status_t
_get_next_thread_info(team_id team, int32* cookie, thread_info* info, size_t size)
{
	if (cookie == NULL || *cookie < 0 || info == NULL
		|| size != sizeof(thread_info)) {
		return B_BAD_VALUE;
	}

	if (team == 0)
		team = getpid();

	ThreadEnumState* state;
	if (*cookie == 0) {
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/task", (int)team);

		state = new (std::nothrow) ThreadEnumState();
		if (state == NULL)
			return B_NO_MEMORY;

		if (!state->batch.Open(path)) {
			delete state;
			return B_BAD_TEAM_ID;
		}

		*cookie = proc_enum_alloc(state);
		if (*cookie == 0) {
			delete state;
			return B_NO_MEMORY;
		}
	} else {
		state = (ThreadEnumState*)proc_enum_state(*cookie);
		if (state == NULL)
			return B_BAD_VALUE;
	}

	pid_t tid;
	while (state->batch.NextPid(&tid)) {
		// Files open relative to the enumeration's own /proc/<team>/task
		// fd, with the tid as a path prefix; no per-tid directory open.
		char prefix[32];
		snprintf(prefix, sizeof(prefix), "%d/", (int)tid);
		if (_GetThreadInfoVia(state->batch.Fd(), prefix, (thread_id)tid,
				info, size) == B_OK) {
			return B_OK;
		}
	}

	delete (ThreadEnumState*)proc_enum_release(*cookie);
	*cookie = 0;
	return B_BAD_VALUE;
}


thread_id
find_thread(const char* name)
{
	if (name == NULL) {
		if (sCachedTid == 0)
			sCachedTid = syscall(SYS_gettid);
		return sCachedTid;
	}

	// Names are stored truncated to 15 chars; truncate the query to match.
	char truncated[16];
	strlcpy(truncated, name, sizeof(truncated));

	team_id team = getpid();
	int32 cookie = 0;
	thread_info info;
	while (_get_next_thread_info(team, &cookie, &info, sizeof(info)) == B_OK) {
		if (strcmp(info.name, truncated) == 0)
			return info.thread;
	}

	return B_NAME_NOT_FOUND;
}


status_t
set_thread_priority(thread_id id, int32 priority)
{
	struct sched_param param = {};

	if (priority == 0) {
		sched_setscheduler(id, SCHED_IDLE, &param);
	} else if (priority >= 1 && priority <= 9) {
		sched_setscheduler(id, SCHED_OTHER, &param);
		setpriority(PRIO_PROCESS, id, 19 - (priority - 1));
	} else if (priority == 10) {
		sched_setscheduler(id, SCHED_OTHER, &param);
		setpriority(PRIO_PROCESS, id, 0);
	} else if (priority >= 11 && priority <= 30) {
		sched_setscheduler(id, SCHED_OTHER, &param);
		setpriority(PRIO_PROCESS, id, -1 - (priority - 11));
	} else if (priority >= 31 && priority <= 120) {
		// Try SCHED_RR — works if CAP_SYS_NICE or RLIMIT_RTPRIO allows it.
		// If not, fall back to nice -20 (highest non-RT priority available
		// without privileges).
		// 31->1, 120->90
		param.sched_priority = 1 + (priority - 31);
		if (sched_setscheduler(id, SCHED_RR, &param) != 0) {
			param.sched_priority = 0;
			sched_setscheduler(id, SCHED_OTHER, &param);
			setpriority(PRIO_PROCESS, id, -20);
		}
	}
	return B_OK;
}


status_t
wait_for_remote_thread(thread_id id, status_t* returnCode)
{
	CALLED();

	if (id <= 0)
		return B_BAD_THREAD_ID;

	int pfd = syscall(SYS_pidfd_open, id, 0);
	if (pfd < 0)
		return B_BAD_THREAD_ID;

	struct pollfd p = { .fd = pfd, .events = POLLIN };
	int pollfd;
	do {
		pollfd = poll(&p, 1, -1);
	} while (pollfd < 0 && errno == EINTR);

	if (pollfd < 0) {
		close(pfd);
		return B_BAD_THREAD_ID;
	}

	status_t exitStatus = B_OK;
	siginfo_t si;
	memset(&si, 0, sizeof(si));

	if (waitid(P_PIDFD, pfd, &si, WEXITED) == 0) {
		if (si.si_code == CLD_EXITED)
			exitStatus = (status_t)si.si_status;
		else if (si.si_code == CLD_KILLED || si.si_code == CLD_DUMPED)
			exitStatus = (status_t)(-si.si_status);
	}

	close(pfd);

	if (returnCode != NULL)
		*returnCode = exitStatus;

	return B_OK;
}


status_t
wait_for_thread(thread_id id, status_t* returnCode)
{
	CALLED();

	if (id < 0)
		return B_BAD_THREAD_ID;

	if (id == find_thread(NULL)) {
		debugger("wait_for_thread: do you really want to wait for yourself?");
		return B_BAD_THREAD_ID;
	}

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();

	// Haiku restarts an interrupted wait_for_thread(); nexus returns
	// B_INTERRUPTED for any signal and for the freezer during suspend.
	struct nexus_thread_waitfor_req exchange;
	int nio;
	do {
		memset(&exchange, 0, sizeof(exchange));
		exchange.receiver = id;
		nio = nexus_io(nexus, NEXUS_THREAD_WAITFOR, &exchange);
	} while (nio == 0 && exchange.ret == B_INTERRUPTED);

	if (nio == 0 && exchange.ret == B_OK) {
		if (returnCode != NULL)
			*returnCode = exchange.return_code;
		return B_OK;
	}

	return wait_for_remote_thread(id, returnCode);
}


// Not supported on V\OS, by design. Documented in OS.h.
status_t
suspend_thread(thread_id id)
{
	UNIMPLEMENTED();
	return B_NOT_SUPPORTED;
}


status_t
resume_thread(thread_id id)
{
	CALLED();

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	status_t ret = nexus_io(nexus, NEXUS_THREAD_RESUME, (void*)(intptr_t)id);
	if (ret == B_BAD_THREAD_ID) {
		if (kill(id, 0) < 0)
			return B_BAD_THREAD_ID;

		if (kill(id, SIGCONT) < 0)
			return B_BAD_THREAD_ID;

		return B_OK;
	}

	return ret;
}


status_t
snooze_until(bigtime_t time, int timeBase)
{
	if (timeBase != B_SYSTEM_TIMEBASE)
		return B_ERROR;

	// system_time() is CLOCK_BOOTTIME. The suspend freezer restarts the sleep transparently,
	// and only a signal handler ends it early, reported as B_INTERRUPTED.
	struct timespec deadline;
	deadline.tv_sec = time / 1000000;
	deadline.tv_nsec = (time % 1000000) * 1000;
	int error = clock_nanosleep(CLOCK_BOOTTIME, TIMER_ABSTIME, &deadline,
		NULL);
	if (error == EINTR)
		return B_INTERRUPTED;
	return error == 0 ? B_OK : B_ERROR;
}


status_t
snooze(bigtime_t time)
{
	if (time <= 0)
		return B_OK;

	return snooze_until(system_time() + time, B_SYSTEM_TIMEBASE);
}


}
