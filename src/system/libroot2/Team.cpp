/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

/* cc1plus predefines _GNU_SOURCE for C++. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Team.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <new>
#include <set>
#include <string>

#include "KernelDebug.h"
#include "syscalls.h"
#include "exec_status.h"
#include "proc_read.h"
#include <syscall_process_info.h>

#include "../kernel/nexus/nexus/nexus.h"


int32 __gCPUCount = BKernelPrivate::Team::GetCPUCount();
mode_t __gUmask = BKernelPrivate::Team::GetUmask();
int __libc_argc = 0;
char** __libc_argv = NULL;
char** argv_save = NULL;


namespace BKernelPrivate {


static pthread_once_t gTeamOnce = PTHREAD_ONCE_INIT;

static int gNexus = -1;
static int gNexusArea = -1;
static int gNexusNodeMonitor = -1;
static struct udev* gUdev = NULL;
static pthread_mutex_t gDevicesLock = PTHREAD_MUTEX_INITIALIZER;

// Pids this team spawned via LoadImage(), reaped opportunistically so they
// don't sit as zombies against RLIMIT_NPROC. Never touches pids we didn't
// create ourselves, so it can't steal children waited on elsewhere (janus).
static pthread_mutex_t gLoadedPidsLock = PTHREAD_MUTEX_INITIALIZER;
static std::set<pid_t> gLoadedPids;
static const size_t kMaxTrackedPids = 256;


// Reaps already-exited tracked pids with WNOHANG. Called with gLoadedPidsLock held.
static void
ReapLoadedPidsLocked()
{
	for (std::set<pid_t>::iterator it = gLoadedPids.begin();
			it != gLoadedPids.end(); ) {
		int status;
		pid_t result = waitpid(*it, &status, WNOHANG);
		if (result == *it || (result == -1 && errno == ECHILD))
			gLoadedPids.erase(it++);
		else
			++it;
	}
}

static void
OpenNexusDevices()
{
	pthread_mutex_lock(&gDevicesLock);

	if (gNexus < 0)
		gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
	if (gNexusArea < 0)
		gNexusArea = open("/dev/nexus_area", O_RDWR | O_CLOEXEC);
	if (gNexusNodeMonitor < 0)
		gNexusNodeMonitor = open("/dev/nexus_node_monitor", O_RDWR | O_CLOEXEC);

	pthread_mutex_unlock(&gDevicesLock);
}


static const char*
signal_to_msg(int sig)
{
	switch (sig) {
		case SIGSEGV:
			return "Segmentation Violation";
		case SIGBUS:
			return "Bus Error";
		case SIGFPE:
			return "Floating Point Exception";
		case SIGILL:
			return "Illegal Instruction";
		case SIGABRT:
			return "Abort";
		case SIGTRAP:
			return "Breakpoint/Trap";

		default:
			return "Unknown Signal";
	}
}


void
fault_handler(int sig, siginfo_t* /*info*/, void* /*context*/)
{
	debugger(signal_to_msg(sig));
}


// Priority <100 is reserved: the team must exist before other constructors.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
void __attribute__ ((constructor(1)))
init_team(int argc, char** argv)
{
	TRACE("init_team() %d\n", argc);

	__libc_argc = argc;
	__libc_argv = argv_save = argv;

	__gCPUCount = BKernelPrivate::Team::GetCPUCount();
	__gUmask = BKernelPrivate::Team::GetUmask();

	pthread_once(&gTeamOnce, &Team::InitTeam);

	TRACE("init_team exit\n");
}


void __attribute__ ((destructor(1)))
deinit_team()
{
	TRACE("deinit_team()\n");

	if (gNexus >= 0) {
		close(gNexus);
		gNexus = -1;
	}
	if (gNexusArea >= 0) {
		close(gNexusArea);
		gNexusArea = -1;
	}
	if (gNexusNodeMonitor >= 0) {
		close(gNexusNodeMonitor);
		gNexusNodeMonitor = -1;
	}
}


#pragma GCC diagnostic pop


void
Team::InitTeam()
{
	TRACE("Team::InitTeam\n");

	// If we don't set this we get no debug output from servers
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	setenv("TARGET_SCREEN", "root", 1);

	// Register once per image: handlers inherit across fork(), but re-running
	// in ReinitChildAtFork() would duplicate the handlers each generation.
	static bool sAtForkRegistered = false;
	if (!sAtForkRegistered) {
		sAtForkRegistered = true;
		pthread_atfork(NULL, NULL, &Team::ReinitChildAtFork);
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = fault_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);

	if (gNexus < 0) {
		gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
		if (gNexus < 0) {
			printf("Can't open Nexus IPC\n");
			exit(-1);
		}
	}

	if (gNexusArea < 0) {
		gNexusArea = open("/dev/nexus_area", O_RDWR | O_CLOEXEC);
		if (gNexusArea < 0) {
			printf("Can't open Nexus Area\n");
			exit(-1);
		}
	}

	if (gNexusNodeMonitor < 0) {
		gNexusNodeMonitor = open("/dev/nexus_node_monitor", O_RDWR | O_CLOEXEC);
		if (gNexusNodeMonitor < 0) {
			printf("Can't open Nexus Node Monitor\n");
			exit(-1);
		}
	}
}


int
Team::GetNexusDescriptor()
{
	if (gNexus == -1)
		OpenNexusDevices();

	return gNexus;
}


int
Team::GetSemDescriptor()
{
	if (gNexus == -1)
		OpenNexusDevices();

	return gNexus;
}


int
Team::GetAreaDescriptor()
{
	if (gNexusArea == -1)
		OpenNexusDevices();

	return gNexusArea;
}


int
Team::GetVRefDescriptor(dev_t* dev)
{
	if (gNexusNodeMonitor == -1 || gNexus == -1)
		OpenNexusDevices();

	if (dev != NULL) {
		struct stat st;
		fstat(gNexus, &st);
		*dev = st.st_rdev;
	}
	return gNexusNodeMonitor;
}


int
Team::GetNodeMonitorDescriptor()
{
	if (gNexusNodeMonitor == -1)
		OpenNexusDevices();

	return gNexusNodeMonitor;
}


struct udev*
Team::GetUDev()
{
	if (gUdev == NULL)
		gUdev = udev_new();

	return gUdev;
}


mode_t
Team::GetUmask()
{
	mode_t m = umask(0);
	umask(m);
	return m;
}


int32
Team::GetCPUCount()
{
	int32 cpuCount = (int32)sysconf(_SC_NPROCESSORS_ONLN);
	return (cpuCount > 0) ? cpuCount : 1;	
}


void
Team::ReinitChildAtFork()
{
	// Reset tid cache before find_thread(NULL) — parent's stale tid must not persist.
	ResetCachedThreadId();

	TRACE("ReinitChildAtFork()\n");

	__gCPUCount = BKernelPrivate::Team::GetCPUCount();
	__gUmask = BKernelPrivate::Team::GetUmask();

	if (gNexus >= 0) {
		close(gNexus);
		gNexus = -1;
	}
	if (gNexusArea >= 0) {
		close(gNexusArea);
		gNexusArea = -1;
	}
	if (gNexusNodeMonitor >= 0) {
		close(gNexusNodeMonitor);
		gNexusNodeMonitor = -1;
	}
	if (gUdev != NULL) {
		udev_unref(gUdev);
		gUdev = NULL;
	}

	// Locks are inherited in whatever state they had at fork time; another
	// thread may have held one, deadlocking the first user of it here.
	pthread_mutex_init(&gLoadedPidsLock, NULL);
	pthread_mutex_init(&gDevicesLock, NULL);

	// Enumeration handles belong to the parent's get_next_* calls.
	proc_enum_reset_after_fork();

	// Inherited pids belong to the parent's LoadImage() calls, not ours;
	// waiting on them here would race the parent's own reaping.
	gLoadedPids.clear();

	// Opening /dev/nexus creates our team; nothing to park on here.
	gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
	if (gNexus < 0) {
		printf("ReinitChildAtFork: Can't open Nexus IPC\n");
		exit(1);
	}

	gTeamOnce = PTHREAD_ONCE_INIT;
	pthread_once(&gTeamOnce, &Team::InitTeam);
}


thread_id
Team::LoadImage(int32 argc, const char** argv, const char** envp,
	int* _execStatusFd)
{
	if (argc < 1 || argv == NULL || argv[0] == NULL) {
		TRACE("load_image: invalid arguments\n");
		return B_BAD_VALUE;
	}

	if (_execStatusFd != NULL)
		*_execStatusFd = -1;

	TRACE("load_image: %s (argc=%d)\n", argv[0], argc);

	// An unresolvable dependency is reported here, before the clone; the
	// loader's list mode resolves libraries without running the app.
	char loaderDetail[256];
	status_t loaderStatus = CheckDynamicLoader(argv[0], loaderDetail,
		sizeof(loaderDetail));
	if (loaderStatus < B_OK) {
		fprintf(stderr, "load_image: %s: %s%s%s\n", argv[0],
			strerror(loaderStatus), loaderDetail[0] != '\0' ? ": " : "",
			loaderDetail);
		return loaderStatus;
	}

	int statusPipe[2] = { -1, -1 };
	if (_execStatusFd != NULL && pipe2(statusPipe, O_CLOEXEC) != 0)
		return B_NO_MORE_FDS;

	TRACE("LoadImage: about to clone(), gNexus=%d gNexusArea=%d gNexusNodeMonitor=%d\n",
		gNexus, gNexusArea, gNexusNodeMonitor);

	pthread_mutex_lock(&gLoadedPidsLock);
	ReapLoadedPidsLocked();
	pthread_mutex_unlock(&gLoadedPidsLock);

	pid_t pid = syscall(SYS_clone, SIGCHLD, 0, NULL, NULL, 0);
	if (pid == -1) {
		TRACE("load_image: clone failed: %s\n", strerror(errno));
		fprintf(stderr, "load_image: clone failed: %s\n", strerror(errno));
		CloseExecStatusPipe(statusPipe[0], statusPipe[1]);
		return errno == EAGAIN ? B_NO_MORE_TEAMS : B_ERROR;
	}

	if (pid == 0) {
		if (statusPipe[0] >= 0)
			close(statusPipe[0]);

		TRACE("LoadImage CHILD: closing nexus fds gNexus=%d gNexusArea=%d gNexusNodeMonitor=%d\n",
			gNexus, gNexusArea, gNexusNodeMonitor);

		if (gNexus >= 0) {
			close(gNexus);
			gNexus = -1;
		}
		if (gNexusArea >= 0) {
			close(gNexusArea);
			gNexusArea = -1;
		}
		if (gNexusNodeMonitor >= 0) {
			close(gNexusNodeMonitor);
			gNexusNodeMonitor = -1;
		}
		if (gUdev != NULL) {
			udev_unref(gUdev);
			gUdev = NULL;
		}

		// Deliberately no O_CLOEXEC: nexus_release() tears the team down
		// when open_count hits 0, which would fire in the window between
		// this open() and the exec below if the fd carried CLOEXEC.
		gNexus = open("/dev/nexus", O_RDWR);

		int nexus = BKernelPrivate::Team::GetNexusDescriptor();

		// Park until the creator's resume_thread().
		thread_id id = nexus_io(nexus, NEXUS_THREAD_CLONE_EXECUTED, NULL);
		if (id < 0) {
			if (statusPipe[1] >= 0) {
				signal(SIGPIPE, SIG_IGN);
				status_t status = id;
				write(statusPipe[1], &status, sizeof(status));
			}
			fprintf(stderr, "load_image child: nexus park failed (%d)\n",
				(int)id);
			_exit(127);
		}

		// execvpe() would fall back to /bin/sh on ENOEXEC, launching a
		// 0-byte or text file as a script; real paths get no such tail.
		if (strchr(argv[0], '/') != NULL) {
			execve(argv[0], const_cast<char* const*>(argv),
				envp ? const_cast<char* const*>(envp) : environ);
		} else {
			execvpe(argv[0], const_cast<char* const*>(argv),
				envp ? const_cast<char* const*>(envp) : environ);
		}

		int execErrno = errno;

		// The CLOEXEC write end closing on a successful exec is what tells
		// the creator the image is up; a failure must carry its errno.
		if (statusPipe[1] >= 0) {
			// the creator may have given up on the pipe (suspended
			// launch); the write must not kill us before the log line
			signal(SIGPIPE, SIG_IGN);
			status_t status = -execErrno;
			write(statusPipe[1], &status, sizeof(status));
		}

		fprintf(stderr, "load_image child: exec failed for '%s': %s\n",
			argv[0], strerror(execErrno));

		 // command not found
		_exit(127);
	}

	if (statusPipe[1] >= 0)
		close(statusPipe[1]);

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	// Creator-side registration: nexus pre-creates the child's team, so a
	// resume_thread() that lands before the child opens /dev/nexus is kept.
	thread_id id = nexus_io(nexus, NEXUS_THREAD_REGISTER, (void*)(intptr_t)pid);
	if (id < 0) {
		// The child may be parked for a resume that never comes.
		printf("Fork failed\n");
		kill(pid, SIGKILL);
		pthread_mutex_lock(&gLoadedPidsLock);
		if (gLoadedPids.size() < kMaxTrackedPids)
			gLoadedPids.insert(pid);
		pthread_mutex_unlock(&gLoadedPidsLock);
		CloseExecStatusPipe(statusPipe[0], -1);
		return B_BAD_THREAD_ID;
	}

	pthread_mutex_lock(&gLoadedPidsLock);
	if (gLoadedPids.size() >= kMaxTrackedPids)
		ReapLoadedPidsLocked();
	if (gLoadedPids.size() < kMaxTrackedPids)
		gLoadedPids.insert(pid);
	pthread_mutex_unlock(&gLoadedPidsLock);

	if (_execStatusFd != NULL)
		*_execStatusFd = statusPipe[0];

	TRACE("load_image: success\n");
	return id;
}

}


extern "C" {


status_t
_get_team_usage_info(team_id team, int32 who, team_usage_info* info, size_t size)
{
	if (info == NULL || size != sizeof(team_usage_info) || team < 0)
		return B_BAD_VALUE;

	if (team == 0)
		team = getpid();

	info->user_time = 0;
	info->kernel_time = 0;

	// The pinned /proc/<team> directory keeps a reused pid out of the read.
	struct proc_dir dir;
	if (!proc_dir_open_pid(&dir, (pid_t)team))
		return B_BAD_TEAM_ID;

	int fd = proc_dir_open_file(&dir, "stat");
	proc_dir_close(&dir);
	if (fd < 0)
		return B_BAD_TEAM_ID;

	char buffer[1024];
	ssize_t dataRead = proc_read_all(fd, buffer, sizeof(buffer));
	close(fd);
	if (dataRead <= 0)
		return B_ERROR;

	struct proc_stat_info stat;
	if (!proc_parse_stat(buffer, &stat))
		return B_ERROR;

	long hz = proc_clk_tck();
	info->user_time = (bigtime_t)((stat.utimeTicks * 1000000ULL)
		/ (uint64)hz);
	info->kernel_time = (bigtime_t)((stat.stimeTicks * 1000000ULL)
		/ (uint64)hz);

	return B_OK;
}


status_t
kill_team(team_id team)
{
	int err = kill((pid_t) team, SIGKILL);
	if (err < 0)
		return B_BAD_TEAM_ID;

	return B_OK;
}


static status_t _GetTeamInfoFrom(int dirFd, const char* prefix, team_id id,
	team_info* info, size_t size);


static int
_OpenPrefixed(int dirFd, const char* prefix, const char* name)
{
	char path[64];
	snprintf(path, sizeof(path), "%s%s", prefix, name);
	return proc_openat_file(dirFd, path);
}


status_t
_get_team_info(team_id id, team_info* info, size_t size)
{
	if (id < 0 || info == NULL || size != sizeof(team_info))
		return B_BAD_VALUE;

	// id==0 means "current team" in Haiku — resolve before any /proc checks
	if (id == 0)
		id = getpid();

	proc_refresh_boot_time();

	struct proc_dir dir;
	if (!proc_dir_open_pid(&dir, (pid_t)id))
		return B_BAD_TEAM_ID;

	status_t result = _GetTeamInfoFrom(dir.dirFd, "", id, info, size);
	proc_dir_close(&dir);
	return result;
}


// dirFd is the pinned /proc/<id>, or the walk's /proc fd with prefix "<id>/".
static status_t
_GetTeamInfoFrom(int dirFd, const char* prefix, team_id id, team_info* info,
	size_t size)
{
	memset(info, 0, sizeof(team_info));
	info->team = id;

	char buffer[2048];

	int statusFd = _OpenPrefixed(dirFd, prefix, "status");
	if (statusFd < 0)
		return B_BAD_TEAM_ID;

	if (proc_read_all(statusFd, buffer, sizeof(buffer)) > 0) {
		struct proc_status_info status;
		if (proc_parse_status(buffer, &status)) {
			strlcpy(info->name, status.name, sizeof(info->name));
			info->real_uid = status.realUid;
			info->uid = status.effectiveUid;
			info->real_gid = status.realGid;
			info->gid = status.effectiveGid;
			info->thread_count = status.threadCount;
			info->parent = (team_id)status.ppid;
		}
	}
	close(statusFd);

	info->argc = 0;
	info->args[0] = '\0';

	int cmdFd = _OpenPrefixed(dirFd, prefix, "cmdline");
	if (cmdFd >= 0) {
		ssize_t length = proc_read_all(cmdFd, buffer, sizeof(buffer));
		close(cmdFd);

		if (length > 0) {
			int argc = 0;
			for (ssize_t i = 0; i < length; i++) {
				unsigned char c = (unsigned char)buffer[i];
				if (c == '\0') {
					argc++;
					buffer[i] = ' ';
				} else if (c < 0x20 || c == 0x7f) {
					// Processes can poke arbitrary bytes into their cmdline.
					buffer[i] = ' ';
				}
			}

			while (length > 0 && buffer[length - 1] == ' ')
				length--;
			buffer[length] = '\0';

			if (length > 0) {
				info->argc = (argc > 0) ? argc : 1;
				strlcpy(info->args, buffer, sizeof(info->args));
			}
		}
	}

	// Kernel threads have an empty cmdline; fall back to Name: in brackets
	// so callers that only look at args don't render a blank row.
	if (info->args[0] == '\0' && info->name[0] != '\0') {
		snprintf(info->args, sizeof(info->args), "[%s]", info->name);
		info->argc = 1;
	}

	ProcLineReader maps;
	if (maps.OpenFd(_OpenPrefixed(dirFd, prefix, "maps"))) {
		int images = 0;
		int areas = 0;
		char lastPath[PATH_MAX] = {0};
		size_t lastPathLength = 0;
		const char* line;
		while ((line = maps.Next()) != NULL) {
			// Only file-backed lines can be images; skip the parse otherwise.
			areas++;
			if (strchr(line, '/') == NULL)
				continue;

			struct proc_map_line map;
			if (!proc_parse_map_header(line, &map))
				continue;
			if (map.pathLength == 0 || map.path[0] != '/')
				continue;

			// Consecutive mappings of the same file count as one image,
			// matching the previous lastPath dedup.
			if (lastPathLength != map.pathLength
				|| memcmp(map.path, lastPath, map.pathLength) != 0) {
				images++;
				lastPathLength = map.pathLength;
				if (lastPathLength > sizeof(lastPath) - 1)
					lastPathLength = sizeof(lastPath) - 1;
				memcpy(lastPath, map.path, lastPathLength);
				lastPath[lastPathLength] = '\0';
			}
		}
		info->image_count = images;
		info->area_count = areas;
	}

	info->debugger_nub_thread = -1;
	info->debugger_nub_port = 0;

	int statFd = _OpenPrefixed(dirFd, prefix, "stat");
	if (statFd >= 0) {
		char statBuffer[1024];
		ssize_t length = proc_read_all(statFd, statBuffer,
			sizeof(statBuffer));
		close(statFd);

		struct proc_stat_info stat;
		if (length > 0 && proc_parse_stat(statBuffer, &stat)) {
			info->group_id = stat.pgrp;
			info->session_id = stat.session;

			if (stat.startTimeTicks != 0) {
				uint64 bootTime = proc_boot_time_micros();
				if (bootTime != 0) {
					double startSeconds
						= (double)stat.startTimeTicks
							/ (double)proc_clk_tck();
					info->start_time = (bigtime_t)(bootTime
						+ (uint64)(startSeconds * 1e6));
				}
			}
		}
	}

	return B_OK;
}


namespace {


struct TeamEnumState {
	ProcDirBatch	batch;
};


} // namespace


status_t
_get_next_team_info(int32* cookie, team_info* info, size_t size)
{
	if (cookie == NULL || *cookie < 0 || info == NULL
		|| size != sizeof(team_info)) {
		return B_BAD_VALUE;
	}

	TeamEnumState* state;
	if (*cookie == 0) {
		state = new (std::nothrow) TeamEnumState();
		if (state == NULL)
			return B_NO_MEMORY;

		if (!state->batch.Open("/proc")) {
			delete state;
			return B_ERROR;
		}

		*cookie = proc_enum_alloc(state);
		if (*cookie == 0) {
			delete state;
			return B_NO_MEMORY;
		}
		// One refresh covers every team of the walk.
		proc_refresh_boot_time();
	} else {
		state = (TeamEnumState*)proc_enum_state(*cookie);
		if (state == NULL)
			return B_BAD_VALUE;
	}

	pid_t pid;
	while (state->batch.NextPid(&pid)) {
		// Files open relative to the enumeration's own /proc fd, with the
		// pid as a path prefix; no per-team directory open.
		char prefix[32];
		snprintf(prefix, sizeof(prefix), "%d/", (int)pid);
		if (_GetTeamInfoFrom(state->batch.Fd(), prefix, (team_id)pid, info,
				size) == B_OK) {
			return B_OK;
		}
	}

	delete (TeamEnumState*)proc_enum_release(*cookie);
	*cookie = -1;
	return B_BAD_VALUE;
}


status_t
get_memory_properties(team_id teamID, const void* address, uint32* _protected,
	 uint32* _lock)
{
	if (_protected == NULL || _lock == NULL)
		return B_BAD_VALUE;

	*_protected = 0;
	*_lock = 0;

	if (teamID < 0 || address == NULL)
		return B_BAD_VALUE;

	if (teamID == 0)
		teamID = getpid();

	struct proc_dir dir;
	if (!proc_dir_open_pid(&dir, (pid_t)teamID))
		return B_BAD_TEAM_ID;

	unsigned long long addr = (unsigned long long)(uintptr_t)address;
	unsigned long long start = 0;
	unsigned long long end = 0;
	char perms[8] = {0};
	bool found = false;

	ProcLineReader maps;
	if (!maps.Open(&dir, "maps")) {
		proc_dir_close(&dir);
		return B_BAD_TEAM_ID;
	}

	const char* line;
	while ((line = maps.Next()) != NULL) {
		struct proc_map_line map;
		if (!proc_parse_map_header(line, &map))
			continue;

		if (addr >= map.start && addr < map.end) {
			found = true;
			start = map.start;
			end = map.end;
			strlcpy(perms, map.perms, sizeof(perms));
			break;
		}
	}
	maps.Close();

	if (!found) {
		proc_dir_close(&dir);
		return B_BAD_VALUE;
	}

	uint32 prot = 0;
	if (perms[0] == 'r') prot |= B_READ_AREA;
	if (perms[1] == 'w') prot |= B_WRITE_AREA;
	if (perms[2] == 'x') prot |= B_EXECUTE_AREA;
	*_protected = prot;

	// The Locked: flag lives in smaps, which needs PTRACE rights for other
	// teams; unreadable smaps just leaves the lock unknown (reported 0).
	ProcLineReader smaps;
	if (smaps.Open(&dir, "smaps")) {
		bool inTarget = false;
		while ((line = smaps.Next()) != NULL) {
			struct proc_map_line map;
			if (proc_parse_map_header(line, &map)) {
				inTarget = (map.start == start && map.end == end);
				continue;
			}

			if (inTarget && strncmp(line, "Locked:", 7) == 0) {
				long lockedKb = strtol(line + 7, NULL, 10);
				*_lock = lockedKb > 0 ? 1 : 0;
				break;
			}
		}
	}
	proc_dir_close(&dir);

	return B_OK;
}


pid_t
_kern_process_info(pid_t process, int32 which)
{
	switch (which) {
		case SESSION_ID:
			return getsid(process);
		case GROUP_ID:
			return getpgid(process);
		case PARENT_ID: {
			struct proc_dir dir;
			if (!proc_dir_open_pid(&dir, (pid_t)process))
				return -1;

			int fd = proc_dir_open_file(&dir, "status");
			proc_dir_close(&dir);
			if (fd < 0)
				return -1;

			char buffer[1024];
			if (proc_read_all(fd, buffer, sizeof(buffer)) <= 0) {
				close(fd);
				return -1;
			}
			close(fd);

			struct proc_status_info status;
			if (!proc_parse_status(buffer, &status))
				return -1;
			return status.ppid;
		}
		default:
			return -1;
	}
}


}
