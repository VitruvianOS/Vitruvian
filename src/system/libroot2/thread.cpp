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
#include <signal.h>
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
#include "rtkit.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {


static __thread pthread_key_t sOnExitKey;


struct thread_data {
	char name[B_OS_NAME_LENGTH];

	thread_func func;
	void* data;
	int32 priority;

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


#ifndef SCHED_FLAG_RESET_ON_FORK
#	define SCHED_FLAG_RESET_ON_FORK 0x01
#endif

struct vos_sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flags;
	int32_t sched_nice;
	uint32_t sched_priority;
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
	uint32_t sched_util_min;
	uint32_t sched_util_max;
};


// Priority mapping (legacy 0..120 onto Linux), shared by spawn_thread() and
// set_thread_priority():
//
//   0        SCHED_IDLE
//   1..9     SCHED_OTHER, nice 19..11
//   10       SCHED_OTHER, nice 0 (B_NORMAL_PRIORITY)
//   11..19   SCHED_OTHER, nice -1..-9, EEVDF slice 2.5 ms..0.5 ms
//            (B_DISPLAY_PRIORITY 15: nice -5, 1.5 ms)
//   20..99   SCHED_OTHER, nice -10..-19, slice 1 ms..0.5 ms
//            (B_URGENT_DISPLAY_PRIORITY 20: nice -10, 1 ms)
//   100..120 SCHED_FIFO, rtprio 10..40 (never 99, headroom stays for the
//            kernel and the audio server), SCHED_RESET_ON_FORK.
//            B_REAL_TIME_DISPLAY_PRIORITY 100, B_URGENT_PRIORITY 110,
//            B_REAL_TIME_PRIORITY 120.
//
// A runaway FIFO thread is bounded by RLIMIT_RTTIME (SIGXCPU, default action
// kills the process) on top of the kernel's sched_rt_runtime throttling.
// Lowering always works; raising without CAP_SYS_NICE or RLIMIT_NICE/RTPRIO
// goes through rtkit, and is refused with B_NOT_ALLOWED if rtkit says no.
struct priority_class {
	int policy;
	int nice;
	int rtprio;
	uint64_t sliceNs;
};


static void
MapPriority(int32 priority, priority_class& c)
{
	c.policy = SCHED_OTHER;
	c.nice = 0;
	c.rtprio = 0;
	c.sliceNs = 0;

	if (priority == 0) {
		c.policy = SCHED_IDLE;
	} else if (priority <= 9) {
		c.nice = 19 - (priority - 1);
	} else if (priority == 10) {
		// defaults
	} else if (priority <= 19) {
		c.nice = -1 - (priority - 11);
		c.sliceNs = 2500000 - (priority - 11) * 250000;
	} else if (priority <= 99) {
		c.nice = -10 - ((priority - 20) * 9) / 79;
		c.sliceNs = 1000000 - ((priority - 20) * 500000) / 79;
	} else {
		c.policy = SCHED_FIFO;
		c.rtprio = 10 + ((priority - 100) * 30) / 20;
	}
}


static int
ApplyClass(pid_t tid, const priority_class& c, int nice)
{
	vos_sched_attr attr;
	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.sched_policy = c.policy;
	attr.sched_nice = nice;
	attr.sched_priority = c.rtprio;
	attr.sched_runtime = c.policy == SCHED_OTHER ? c.sliceNs : 0;
	if (c.policy == SCHED_FIFO)
		attr.sched_flags = SCHED_FLAG_RESET_ON_FORK;

	if (syscall(SYS_sched_setattr, tid, &attr, 0) == 0)
		return 0;
	int error = errno;
	if (error == EINVAL && attr.sched_runtime != 0) {
		// kernel without custom slices: retry without
		attr.sched_runtime = 0;
		if (syscall(SYS_sched_setattr, tid, &attr, 0) == 0)
			return 0;
		error = errno;
	}
	if (error == ENOSYS) {
		struct sched_param param = {};
		param.sched_priority = c.rtprio;
		if (sched_setscheduler(tid, c.policy, &param) != 0)
			return errno;
		if (c.policy == SCHED_OTHER && setpriority(PRIO_PROCESS, tid, nice) != 0)
			return errno;
		return 0;
	}
	return error;
}


// Bounds a runaway FIFO thread; rtkit insists on it as well. Once per process.
// Threads put on SCHED_FIFO, for the SIGXCPU handler.
static const int kMaxRealtimeThreads = 64;
static pid_t sRealtimeThreads[kMaxRealtimeThreads];


static void
RememberRealtimeThread(pid_t tid)
{
	for (int i = 0; i < kMaxRealtimeThreads; i++) {
		pid_t expected = 0;
		if (__atomic_load_n(&sRealtimeThreads[i], __ATOMIC_RELAXED) == tid)
			return;
		if (__atomic_compare_exchange_n(&sRealtimeThreads[i], &expected, tid,
				false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
			return;
		}
	}
}


// Async signal safe: raw syscalls only.
static void
DemoteRealtimeThreads(int)
{
	int savedErrno = errno;
	struct sched_param param = {};
	for (int i = 0; i < kMaxRealtimeThreads; i++) {
		pid_t tid = __atomic_load_n(&sRealtimeThreads[i], __ATOMIC_RELAXED);
		if (tid != 0 && syscall(SYS_sched_getscheduler, tid) == SCHED_FIFO)
			syscall(SYS_sched_setscheduler, tid, SCHED_OTHER, &param);
	}
	errno = savedErrno;
}


static void
LimitRealtimeTime()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	// SIGXCPU at the soft limit demotes the runaway threads, so the hard
	// limit's SIGKILL is never reached. An app's own handler is kept.
	struct sigaction old;
	if (sigaction(SIGXCPU, NULL, &old) == 0 && old.sa_handler == SIG_DFL) {
		struct sigaction action = {};
		action.sa_handler = DemoteRealtimeThreads;
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_RESTART;
		sigaction(SIGXCPU, &action, NULL);
	}

	long long limit = RtkitRTTimeMax();
	struct rlimit rl;
	if (getrlimit(RLIMIT_RTTIME, &rl) != 0)
		return;
	if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max <= (rlim_t)limit
		&& rl.rlim_cur <= rl.rlim_max / 2) {
		return;
	}
	rl.rlim_max = (rlim_t)limit;
	rl.rlim_cur = (rlim_t)limit / 2;
	setrlimit(RLIMIT_RTTIME, &rl);
}


static status_t
ApplyPriority(pid_t tid, int32 priority)
{
	if (priority < 0 || priority > 120)
		return B_BAD_VALUE;

	priority_class c;
	MapPriority(priority, c);

	if (c.policy == SCHED_FIFO)
		LimitRealtimeTime();

	int error = ApplyClass(tid, c, c.nice);
	if (error == 0) {
		if (c.policy == SCHED_FIFO)
			RememberRealtimeThread(tid);
		return B_OK;
	}
	if (error == ESRCH)
		return B_BAD_THREAD_ID;
	if (error != EPERM && error != EACCES)
		return B_ERROR;

	// Refused: ask rtkit, otherwise keep the closest allowed setting.
	int granted = 0;
	if (c.policy == SCHED_FIFO) {
		if (RtkitMakeRealtime(tid, c.rtprio, &granted) == 0) {
			RememberRealtimeThread(tid);
			return B_OK;
		}
		priority_class high;
		MapPriority(19, high);
		c = high;
	}

	if (RtkitMakeHighPriority(tid, c.nice, &granted) == 0) {
		// slice only, nice is already what rtkit granted
		if (ApplyClass(tid, c, granted) != 0 && c.sliceNs != 0) {
			priority_class plain = c;
			plain.sliceNs = 0;
			ApplyClass(tid, plain, granted);
		}
		return granted == c.nice && priority < 100 ? B_OK : B_NOT_ALLOWED;
	}

	// Fall back to normal with whatever nice the process may still use.
	priority_class normal;
	MapPriority(10, normal);
	ApplyClass(tid, normal, 0);
	return B_NOT_ALLOWED;
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

	// A thread can always lower itself; raising may go through rtkit.
	if (threadData->priority != 10)
		ApplyPriority(threadData->tid, threadData->priority);

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


// Approximate inverse of MapPriority(): reports what is really set, which
// can differ from the request after a refused raise or an rtkit grant.
// Anything unreadable falls back to 10 rather than guess.
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
		if (sched_getparam((pid_t)id, &param) != 0)
			return 10;
		int rt = param.sched_priority;
		if (rt <= 10)
			return 100;
		if (rt >= 40)
			return 120;
		return 100 + ((rt - 10) * 20) / 30;
	}

	errno = 0;
	int nice = getpriority(PRIO_PROCESS, (id_t)id);
	if (nice == -1 && errno != 0)
		return 10;

	if (nice == 0)
		return 10;
	if (nice > 0 && nice <= 19)
		return 20 - nice;
	if (nice <= -1 && nice >= -9)
		return 10 - nice;
	if (nice <= -10 && nice >= -20) {
		int priority = 20 + ((-10 - nice) * 79) / 9;
		return priority > 99 ? 99 : priority;
	}

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
	threadData->priority = priority;
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
	if (id <= 0)
		return B_BAD_THREAD_ID;
	return BKernelPrivate::ApplyPriority((pid_t)id, priority);
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
