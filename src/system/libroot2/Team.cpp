/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#define _GNU_SOURCE

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

#include <string>

#include "KernelDebug.h"
#include "syscalls.h"
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
fault_handler(int sig)
{
	debugger(signal_to_msg(sig));
}


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

void
Team::InitTeam()
{
	TRACE("Team::InitTeam\n");

	// If we don't set this we get no debug output from servers
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	setenv("TARGET_SCREEN", "root", 1);

	// Register atfork handlers. This should be good for us - we register the
	// first set of callbacks that will be executed before any other set the
	// user may register. Calling pthread_atfork again adds to the list.
	pthread_atfork(&Team::PrepareFatherAtFork,
		&Team::SyncFatherAtFork, &Team::ReinitChildAtFork);

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
Team::PrepareFatherAtFork()
{
	TRACE("PrepareFatherAtFork()\n");
}


void
Team::SyncFatherAtFork()
{
	TRACE("SyncFatherAtFork()\n");

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	thread_id id = nexus_io(nexus, NEXUS_THREAD_WAIT_NEWBORN, NULL);
	if (id < 0)
		printf("Fork failed\n");
}


void
Team::ReinitChildAtFork()
{
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

	gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
	if (gNexus < 0) {
		printf("ReinitChildAtFork: Can't open Nexus IPC\n");
		exit(1);
	}

	thread_id id = nexus_io(gNexus, NEXUS_THREAD_CLONE_EXECUTED, (void*)1);
	if (id < 0) {
		printf("ReinitChildAtFork: clone failed (%d)\n", (int)id);
		exit(1);
	}

	gTeamOnce = PTHREAD_ONCE_INIT;
	pthread_once(&gTeamOnce, &Team::InitTeam);
}


thread_id
Team::LoadImage(int32 argc, const char** argv, const char** envp)
{
	if (argc < 1 || argv == NULL || argv[0] == NULL) {
		TRACE("load_image: invalid arguments\n");
		return B_BAD_VALUE;
	}

	TRACE("load_image: %s (argc=%d)\n", argv[0], argc);

	TRACE("LoadImage: about to clone(), gNexus=%d gNexusArea=%d gNexusNodeMonitor=%d\n",
		gNexus, gNexusArea, gNexusNodeMonitor);

	pid_t pid = syscall(SYS_clone, SIGCHLD, 0, NULL, NULL, 0);
	if (pid == -1) {
		TRACE("load_image: clone failed: %s\n", strerror(errno));
		return -errno;
	}

	if (pid == 0) {
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

		gNexus = open("/dev/nexus", O_RDWR);

		int nexus = BKernelPrivate::Team::GetNexusDescriptor();
		thread_id id = nexus_io(nexus, NEXUS_THREAD_CLONE_EXECUTED, NULL);
		if (id < 0)
			printf("Fork failed\n");

		execvpe(argv[0], const_cast<char* const*>(argv),
			envp ? const_cast<char* const*>(envp) : environ);

		TRACE("load_image child: exec failed for '%s': %s\n",
			argv[0], strerror(errno));

		 // command not found
		_exit(127);
	}

	int nexus = BKernelPrivate::Team::GetNexusDescriptor();
	thread_id id = nexus_io(nexus, NEXUS_THREAD_WAIT_NEWBORN, NULL);
	if (id < 0) {
		printf("Fork failed\n");
		return B_BAD_THREAD_ID;
	}

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

	int pidfd = (int)syscall(SYS_pidfd_open, (pid_t)team, 0);

	if (pidfd < 0) {
		char procdir[64];
		snprintf(procdir, sizeof(procdir), "/proc/%d", team);
		if (access(procdir, F_OK) != 0)
			return B_BAD_TEAM_ID;
	}

	char path[64];
	char buf[4096];

	snprintf(path, sizeof(path), "/proc/%d/stat", team);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (pidfd >= 0)
			close(pidfd);
		return B_BAD_TEAM_ID;
	}

	ssize_t dataRead = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (pidfd >= 0)
		close(pidfd);
	if (dataRead <= 0)
		return B_ERROR;

	buf[dataRead] = '\0';

	char* paren = strrchr(buf, ')');
	if (!paren)
		return B_ERROR;

	char* p = paren + 2;
	unsigned long utime_ticks = 0, stime_ticks = 0;
	int field = 3;
	char* save = NULL;
	char* token = strtok_r(p, " ", &save);

	while (token) {
		if (field == 14) {
			utime_ticks = strtoul(token, NULL, 10);
		} else if (field == 15) {
			stime_ticks = strtoul(token, NULL, 10);
			break;
		}
		field++;
		token = strtok_r(NULL, " ", &save);
	}

	long hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		hz = 100;

	info->user_time = (bigtime_t)((utime_ticks * 1000000ULL) / (unsigned long)hz);
	info->kernel_time = (bigtime_t)((stime_ticks * 1000000ULL) / (unsigned long)hz);

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


status_t
_get_team_info(team_id id, team_info* info, size_t size)
{
	#define UID_LINE 8
	#define GID_LINE 9
	#define MAX_BYTES 64
	#define MAX_LINE_LENGTH 1024

	if (id < 0 || info == NULL || size != sizeof(team_info))
		return B_BAD_VALUE;

	// id==0 means "current team" in Haiku — resolve before any /proc checks
	if (id == 0)
		id = getpid();

	int pidfd = (int)syscall(SYS_pidfd_open, (pid_t)id, 0);

	if (pidfd < 0) {
		char procdir[64];
		snprintf(procdir, sizeof(procdir), "/proc/%d", id);  // use id, not pidfd
		if (access(procdir, F_OK) != 0)
			return B_BAD_TEAM_ID;
	}

	memset(info, 0, sizeof(team_info));

	char procPath[B_PATH_NAME_LENGTH];
	char buffer[MAX_LINE_LENGTH];
	char commandProcPath[B_PATH_NAME_LENGTH];

	snprintf(procPath, sizeof(procPath), "/proc/%d/status", id);
	snprintf(commandProcPath, sizeof(commandProcPath), "/proc/%d/cmdline", id);
	FILE* statusFile = fopen(procPath, "r");
	if (statusFile == NULL) {
		if (pidfd >= 0)
			close(pidfd);
		return B_BAD_TEAM_ID;
	}

	info->team = id;
	info->name[0] = '\0';

	while (fgets(buffer, sizeof(buffer), statusFile) != NULL) {
		if (strncmp(buffer, "Name:", 5) == 0) {
			const char* name = buffer + 5;
			while (*name == ' ' || *name == '\t')
				name++;

			size_t length = 0;
			while (length < B_OS_NAME_LENGTH - 1) {
				unsigned char c = (unsigned char)name[length];
				if (c == '\0' || c == '\n' || c == '\r'
					|| c < 0x20 || c == 0x7f) {
					break;
				}
				info->name[length] = name[length];
				length++;
			}
			info->name[length] = '\0';
		} else if (strncmp(buffer, "Uid:", 4) == 0) {
			uid_t ruid = 0, euid = 0, suid = 0, fuid = 0;
			sscanf(buffer + 4, "%u %u %u %u", &ruid, &euid, &suid, &fuid);
			info->real_uid = ruid;
			info->uid = euid;
		} else if (strncmp(buffer, "Gid:", 4) == 0) {
			gid_t rgid = 0, egid = 0, sgid = 0, fgid = 0;
			sscanf(buffer + 4, "%u %u %u %u", &rgid, &egid, &sgid, &fgid);
			info->real_gid = rgid;
			info->gid = egid;
		} else if (strncmp(buffer, "Threads:", 8) == 0) {
			sscanf(buffer + 8, "%d", &info->thread_count);
		} else if (strncmp(buffer, "PPid:", 5) == 0) {
			pid_t ppid = (pid_t)atoi(buffer + 5);
			info->parent = (team_id)ppid;
		}
	}

	fclose(statusFile);

	info->argc = 0;
	info->args[0] = '\0';

	int cmdFd = open(commandProcPath, O_RDONLY | O_CLOEXEC);
	if (cmdFd >= 0) {
		ssize_t length = read(cmdFd, buffer, sizeof(buffer) - 1);
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

	char mapsPath[B_PATH_NAME_LENGTH];
	snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", id);
	FILE* maps = fopen(mapsPath, "r");
	if (maps != NULL) {
		int images = 0;
		int areas = 0;
		char lastPath[PATH_MAX] = {0};
		while (fgets(buffer, sizeof(buffer), maps)) {
			areas++;
			char* p = strchr(buffer, '/');
			if (p != NULL) {
				char* nl = strchr(p, '\n');
				if (nl != NULL)
					*nl = '\0';
				if (lastPath[0] == '\0' || strcmp(p, lastPath) != 0) {
					images++;
					strlcpy(lastPath, p, sizeof(lastPath));
				}
			}
		}
		fclose(maps);
		info->image_count = images;
		info->area_count = areas;
	}

	info->debugger_nub_thread = -1;
	info->debugger_nub_port = 0;

	char statPath[B_PATH_NAME_LENGTH];
	snprintf(statPath, sizeof(statPath), "/proc/%d/stat", id);
	FILE* statFile = fopen(statPath, "r");
	if (statFile != NULL) {
		char statBuffer[4096];
		size_t length = fread(statBuffer, 1, sizeof(statBuffer) - 1, statFile);
		statBuffer[length] = '\0';
		fclose(statFile);

		char* rparen = strrchr(statBuffer, ')');
		if (rparen != NULL) {
			char* saveptr = NULL;
			(void)strtok_r(rparen + 2, " ", &saveptr);
			(void)strtok_r(NULL, " ", &saveptr);

			char* token = strtok_r(NULL, " ", &saveptr);
			if (token != NULL)
				info->group_id = (pid_t)strtol(token, NULL, 10);

			token = strtok_r(NULL, " ", &saveptr);
			if (token != NULL)
				info->session_id = (pid_t)strtol(token, NULL, 10);

			int field = 5;
			while (field < 22 && (token = strtok_r(NULL, " ", &saveptr)))
				field++;

			if (token != NULL) {
				unsigned long long starttime = strtoull(token, NULL, 10);
				if (starttime != 0) {
					long ticks = sysconf(_SC_CLK_TCK);
					if (ticks <= 0)
						ticks = 100;

					double uptime = 0.0;
					FILE* uptimeFile = fopen("/proc/uptime", "r");
					if (uptimeFile != NULL) {
						if (fscanf(uptimeFile, "%lf", &uptime) != 1)
							uptime = 0.0;
						fclose(uptimeFile);
					}

					struct timespec now;
					if (clock_gettime(CLOCK_REALTIME, &now) == 0
						&& uptime > 0.0) {
						double bootTime = now.tv_sec - uptime;
						double startSeconds = (double)starttime / (double)ticks;
						info->start_time
							= (bigtime_t)((bootTime + startSeconds) * 1e6);
					}
				}
			}
		}
	}

	if (pidfd >= 0)
		close(pidfd);
	return B_OK;
}


status_t
_get_next_team_info(int32* cookie, team_info* info, size_t size)
{
	if (cookie == NULL || *cookie < 0 || info == NULL
		|| size != sizeof(team_info)) {
		return B_BAD_VALUE;
	}

	// The cookie holds the open /proc fd between calls, encoded as fd+1 so
	// that 0 still means "start fresh".
	int fd;
	if (*cookie == 0) {
		fd = _kern_open_dir(-1, "/proc");
		if (fd < 0)
			return B_ERROR;
	} else
		fd = *cookie - 1;

	union {
		struct dirent entry;
		char buffer[sizeof(struct dirent) + NAME_MAX + 1];
	} dirent;

	while (true) {
		ssize_t count = _kern_read_dir(fd, &dirent.entry, sizeof(dirent), 1);
		if (count <= 0) {
			_kern_close(fd);
			*cookie = 0;
			return B_BAD_VALUE;
		}

		pid_t pid = (pid_t)atoi(dirent.entry.d_name);
		if (pid <= 0)
			continue;

		if (_get_team_info((team_id)pid, info, size) == B_OK) {
			*cookie = fd + 1;
			return B_OK;
		}
	}
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

	int pidfd = (int)syscall(SYS_pidfd_open, (pid_t)teamID, 0);

	if (pidfd < 0) {
		char procdir[64];
		snprintf(procdir, sizeof(procdir), "/proc/%d", teamID);
		if (access(procdir, F_OK) != 0)
			return B_BAD_TEAM_ID;
	}

	char maps_path[64];
	snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", teamID);
	FILE* maps = fopen(maps_path, "r");
	if (maps == NULL) {
		if (pidfd >= 0)
			close(pidfd);
		return B_BAD_TEAM_ID;
	}

	unsigned long long addr = (unsigned long long)(uintptr_t)address;
	char line[512];
	int found = 0;
	char perms[8] = {0};
	unsigned long long start = 0, end = 0;
	char pathname[PATH_MAX] = {0};

	while (fgets(line, sizeof(line), maps)) {
		// line format: start-end perms offset dev inode pathname
		// e.g. 00400000-0040b000 r-xp 00000000 08:01 131073 /bin/cat
		int fields = sscanf(line, "%llx-%llx %7s %*s %*s %*s %1023[^\n]",
							&start, &end, perms, pathname);
		if (fields >= 3) {
			if (addr >= start && addr < end) {
				found = 1;
				break;
			}
		}
	}

	fclose(maps);

	if (!found) {
		if (pidfd >= 0)
			close(pidfd);
		return B_BAD_VALUE;
	}

	uint32 prot = 0;
	if (perms[0] == 'r') prot |= B_READ_AREA;
	if (perms[1] == 'w') prot |= B_WRITE_AREA;
	if (perms[2] == 'x') prot |= B_EXECUTE_AREA;
	*_protected = prot;

	char smaps_path[64];
	snprintf(smaps_path, sizeof(smaps_path), "/proc/%d/smaps", teamID);
	FILE* smaps = fopen(smaps_path, "r");
	if (smaps != NULL) {
		while (fgets(line, sizeof(line), smaps)) {
			unsigned long long s = 0, e = 0;
			char p[8] = {0};
			int hdr = sscanf(line, "%llx-%llx %7s", &s, &e, p);
			if (hdr >= 2) {
				if (s == start && e == end) {
					while (fgets(line, sizeof(line), smaps)) {
						if (strncmp(line, "Locked:", 7) == 0) {
							long locked_kb = 0;
							if (sscanf(line + 7, "%ld", &locked_kb) == 1) {
								if (locked_kb > 0)
									*_lock = 1;
								else
									*_lock = 0;
							}
							fclose(smaps);
							if (pidfd >= 0)
								close(pidfd);
							return B_OK;
						}
						if (strchr(line, '-') && strstr(line, "kB") == NULL)
							break;
					}
					break;
				} else {
					continue;
				}
			}
		}
		fclose(smaps);
	}

	if (pidfd >= 0)
		close(pidfd);
	*_lock = 0;
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
			char path[64];
			snprintf(path, sizeof(path), "/proc/%d/status", (int)process);
			FILE* f = fopen(path, "r");
			if (f == NULL)
				return -1;
			char line[256];
			pid_t ppid = -1;
			while (fgets(line, sizeof(line), f)) {
				if (strncmp(line, "PPid:", 5) == 0) {
					sscanf(line + 5, "%d", &ppid);
					break;
				}
			}
			fclose(f);
			return ppid;
		}
		default:
			return -1;
	}
}


}
