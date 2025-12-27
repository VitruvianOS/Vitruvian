/*
 *  Copyright 2018-2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "Team.h"

#include <OS.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <list>
#include <string>

#include "KernelDebug.h"
#include "messaging/MessagingService.h"
#include "syscalls.h"
#include "Thread.h"


#define UID_LINE 8
#define GID_LINE 9
#define MAX_BYTES 64
#define MAX_LINE_LENGTH 1024

mode_t __gUmask = 022;
int32 __gCPUCount;
int __libc_argc;
char** __libc_argv;
extern int gLoadImageFD;


namespace BKernelPrivate {


static std::list<int> gTeams;
static int gNexus = -1;
static int gNexusSem = -1;
static int gNexusVRef = -1;

sem_id Team::fForkSem = -1;


void
segv_handler(int sig)
{
	debugger("Guru Meditation");
}


// This function is executed before everything else
// TODO: what about using gcc .init and .fini?
void __attribute__ ((constructor))
init_team(int argc, char** argv)
{
	TRACE("init_team() %d\n", argc);

	// TODO I think there's more stuff that normally
	// is handled by the debugger in BeOS.
	signal(SIGSEGV, segv_handler);

	// Init global stuff
	int32 cpus = (int32) sysconf(_SC_NPROCESSORS_ONLN);
	__gCPUCount = (cpus > 0) ? (int32_t)cpus : 1;
	__libc_argc = argc;
	__libc_argv = argv;

	// Set screen
	setenv("TARGET_SCREEN", "root", 1);

	Team::InitTeam();

	// This should be good for us. We register the first set of callbacks
	// that will be executed before any other set the user may register.
	pthread_atfork(&Team::PrepareFatherAtFork,
		&Team::SyncFatherAtFork, &Team::ReinitChildAtFork);

	// TODO: this has to go
	if (argv[0] != NULL && strcmp(argv[0], "registrar") <= 0)
		init_messaging_service();
}


void __attribute__ ((destructor))
deinit_team()
{
	TRACE("deinit_team()\n");
}


void
Team::InitTeam()
{
	GetNexusDescriptor();
	GetSemDescriptor();
	GetVRefDescriptor();
}


int
Team::GetNexusDescriptor()
{
	if (gNexus == -1) {
		gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
		if (gNexus < 0) {
			printf("Can't open Nexus IPC\n");
			exit(-1);
		}
	}
	return gNexus;
}


int
Team::GetSemDescriptor()
{
	if (gNexusSem == -1) {
		gNexusSem = open("/dev/nexus_sem", O_RDWR | O_CLOEXEC);
		if (gNexusSem < 0) {
			printf("Can't open Nexus Sem\n");
			exit(-1);
		}
	}
	return gNexusSem;
}


int
Team::GetVRefDescriptor(dev_t* dev)
{
	if (gNexusVRef == -1) {
		gNexusVRef = open("/dev/nexus_vref", O_RDWR | O_CLOEXEC);
		if (gNexusVRef < 0) {
			printf("Can't open Nexus VRef\n");
			exit(-1);
		}
	}

	if (dev != NULL) {
		struct stat st;
		fstat(gNexusVRef, &st);
		*dev = st.st_dev;
	}
	return gNexusVRef;
}


void
Team::PrepareFatherAtFork()
{
	fForkSem = create_sem(0, "team_fork_sem");
}


void
Team::SyncFatherAtFork()
{
	acquire_sem(fForkSem);
	delete_sem(fForkSem);
	fForkSem = -1;
}


void
Team::ReinitChildAtFork()
{
	printf("reinit_at_fork()\n");
	InitTeam();

	Thread::ReinitChildAtFork();

	// _kern_unblock_thread(father)
	release_sem(fForkSem);
	fForkSem = -1;
}


thread_id
Team::LoadImage(int32 argc, const char** argv, const char** envp)
{
	TRACE("load_image: %s\n", argv[0]);

	int lockfd[2];
	if (pipe(lockfd) == -1)
		return -1;

	pid_t pid = fork();
	if (pid == 0) {
		close(lockfd[1]);

		// Wait for the father to unlock us
		uint32 value;
		read(lockfd[0], &value, sizeof(value));

		int32 ret = execvpe(argv[0], const_cast<char* const*>(argv),
			const_cast<char* const*>(envp));
		if (ret == -1) {
			TRACE("execv err %s\n", strerror(errno));
			_exit(1);
		}
	}
	close(lockfd[0]);

	if (pid == -1) {
		close(lockfd[1]);
		TRACE("fork error\n");
		return B_ERROR;
	} else
		gLoadImageFD = lockfd[1];

	return pid;
}


status_t
Team::WaitForTeam(team_id id, status_t* returnCode)
{
	if (id == getpid())
		debugger("Team::WaitForTeam: This shouldn't happen.");

	if (kill(id, 0) < 0)
		return B_BAD_THREAD_ID;

	if (gLoadImageFD != -1) {
		uint32 value = 1;
		write(gLoadImageFD, (const void*)&value, sizeof(uint32));
		close(gLoadImageFD);
		gLoadImageFD = -1;
	}

	int status;
	do {
		TRACE("waitpid %d\n", id);

		if (waitpid(id, &status, WUNTRACED | WCONTINUED) == -1)
			return B_BAD_THREAD_ID;

		if (WIFEXITED(status)) {
			TRACE("waitpid: exited, status=%d\n", WEXITSTATUS(status));
			if (returnCode)
				*returnCode = -WEXITSTATUS(status) == 0 ? B_OK : B_ERROR;
			return B_OK;
		} else if (WIFSIGNALED(status)) {
			TRACE("waitpid: killed by signal %d\n", WTERMSIG(status));
			return B_INTERRUPTED;
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	//}
	return B_BAD_THREAD_ID;
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
		close(pidfd);
		return B_BAD_TEAM_ID;
	}

	ssize_t dataRead = read(fd, buf, sizeof(buf) - 1);
	close(fd);
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
	if (id < 0 || info == NULL || size != sizeof(team_info))
		return B_BAD_VALUE;

	int pidfd = (int)syscall(SYS_pidfd_open, (pid_t)id, 0);

	if (pidfd < 0) {
		char procdir[64];
		snprintf(procdir, sizeof(procdir), "/proc/%d", pidfd);
		if (access(procdir, F_OK) != 0)
			return B_BAD_TEAM_ID;
	}

	memset(info, 0, sizeof(team_info));

	char procPath[B_PATH_NAME_LENGTH];
	char buffer[MAX_LINE_LENGTH];
	char commandProcPath[B_PATH_NAME_LENGTH];

	if (id == 0)
		id = getpid();

	snprintf(procPath, sizeof(procPath), "/proc/%d/status", id);
	snprintf(commandProcPath, sizeof(commandProcPath), "/proc/%d/cmdline", id);
	FILE* statusFile = fopen(procPath, "r");
	if (statusFile == NULL) {
		close(pidfd);
		return B_BAD_TEAM_ID;
	}

	info->team = id;
	while (fgets(buffer, sizeof(buffer), statusFile) != NULL) {
		if (strncmp(buffer, "Uid:", 4) == 0) {
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

	int cmdFd = open(commandProcPath, O_RDONLY);
	if (cmdFd >= 0) {
		ssize_t n = read(cmdFd, buffer, sizeof(buffer) - 1);
		if (n > 0) {
			int argc = 0;
			for (ssize_t i = 0; i < n; i++) {
				if (buffer[i] == '\0') {
					argc++;
					buffer[i] = ' ';
				}
			}
			if (n > 0 && argc == 0)
				argc = 1;
			buffer[n] = '\0';

			info->argc = argc;
			strncpy(info->args, buffer, sizeof(info->args) - 1);
			info->args[sizeof(info->args) - 1] = '\0';
		}
		close(cmdFd);
	} else {
		info->argc = 0;
		info->args[0] = '\0';
	}

	{
		char mapsPath[B_PATH_NAME_LENGTH];
		snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", id);
		FILE* maps = fopen(mapsPath, "r");
		if (maps) {
			int images = 0;
			int areas = 0;
			char lastPath[PATH_MAX] = {0};
			while (fgets(buffer, sizeof(buffer), maps)) {
				areas++;
				char* p = strchr(buffer, '/');
				if (p) {
					char* nl = strchr(p, '\n');
					if (nl) *nl = '\0';
					if (lastPath[0] == '\0' || strcmp(p, lastPath) != 0) {
						images++;
						strncpy(lastPath, p, sizeof(lastPath) - 1);
						lastPath[sizeof(lastPath) - 1] = '\0';
					}
				}
			}
			fclose(maps);
			info->image_count = images;
			info->area_count = areas;
		} else {
			info->image_count = 0;
			info->area_count = 0;
		}
	}

	info->debugger_nub_thread = -1;
	info->debugger_nub_port = 0;

	{
		char statPath[B_PATH_NAME_LENGTH];
		snprintf(statPath, sizeof(statPath), "/proc/%d/stat", id);
		FILE* stat = fopen(statPath, "r");
		if (stat) {
			char statbuf[4096];
			size_t r = fread(statbuf, 1, sizeof(statbuf) - 1, stat);
			statbuf[r] = '\0';
			char* rp = strrchr(statbuf, ')');
			if (rp) {
				char* rest = rp + 2;
				char* saveptr = NULL;
				(void)strtok_r(rest, " ", &saveptr);
				(void)strtok_r(NULL, " ", &saveptr);
				char* tok = strtok_r(NULL, " ", &saveptr);
				if (tok) info->group_id = (pid_t)strtol(tok, NULL, 10);
				tok = strtok_r(NULL, " ", &saveptr);
				if (tok) info->session_id = (pid_t)strtol(tok, NULL, 10);

				int field = 5;
				while (field < 22 && (tok = strtok_r(NULL, " ", &saveptr)))
					field++;
				if (tok) {
					unsigned long long starttime = strtoull(tok, NULL, 10);
					if (starttime != 0) {
						long ticks = sysconf(_SC_CLK_TCK);
						if (ticks <= 0) ticks = 100;

						FILE* uptime = fopen("/proc/uptime", "r");
						double up = 0.0;
						if (uptime) {
							if (fscanf(uptime, "%lf", &up) != 1)
								up = 0.0;
							fclose(uptime);
						}

						struct timespec now;
						if (clock_gettime(CLOCK_REALTIME, &now) == 0 && up > 0.0) {
							double boot_time = now.tv_sec - up;
							double proc_start_secs = ((double)starttime) / (double)ticks;
							double proc_start_time = boot_time + proc_start_secs;
							info->start_time = (bigtime_t)(proc_start_time * 1e6);
						}
					}
				}
			}
			fclose(stat);
		}
	}

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

	if (*cookie == 0) {
		DIR* procDir = opendir("/proc");
		struct dirent* dir = NULL;

		if (procDir != NULL) {
			while ((dir = readdir(procDir)) != NULL) {
				if (atoi(dir->d_name) > 0)
					BKernelPrivate::gTeams.push_front(atoi(dir->d_name));
			}
		} else
			return B_ERROR;

		closedir(procDir);
	}

	if (*cookie >= (int32)BKernelPrivate::gTeams.size())
		return B_BAD_VALUE;

	std::list<int>::iterator gTeamsIt = BKernelPrivate::gTeams.begin();
	std::advance(gTeamsIt, *cookie);
	unsigned int cur_pid = *gTeamsIt;
	(*cookie)++;

	return get_team_info(cur_pid, info);

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

	close(pidfd);
	*_lock = 0;
	return B_OK;
}


status_t
_kern_get_extended_team_info(team_id teamID, uint32 flags,
	void* buffer, size_t size, size_t* _sizeNeeded)
{
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
}


}
