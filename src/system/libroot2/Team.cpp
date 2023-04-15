/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "Team.h"

#include <OS.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <list>
#include <string>

#include "Thread.h"

#define DEBUG 3
#include "KernelDebug.h"
#include "messaging/MessagingService.h"
#include "syscalls.h"


#define UID_LINE 8
#define GID_LINE 9
#define MAX_BYTES 64
#define MAX_LINE_LENGTH 1024

mode_t __gUmask = 022;
int32 __gCPUCount;
int __libc_argc;
char** __libc_argv;


namespace BKernelPrivate {


static std::list<int> gTeams;
static int gNexus = -1;


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
	TRACE("init_team()\n");

	signal(SIGSEGV, segv_handler);

	// Init global stuff
	__gCPUCount = sysconf(_SC_NPROCESSORS_ONLN);
	__libc_argc = argc;
	__libc_argv = argv;

	// Set screen
	setenv("TARGET_SCREEN", "root", 1);

	Team::InitTeam();

	init_ports();

	pthread_atfork(NULL, NULL, &Team::ReinitAtFork);

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
	gNexus = open("/dev/nexus", O_RDWR | O_CLOEXEC);
	if (gNexus < 0) {
		printf("Can't open Nexus IPC\n");
		exit(-1);
	}
}


void
Team::DeInitTeam()
{
	close(gNexus);
}


int
Team::GetNexusDescriptor()
{
	return gNexus;
}


void
Team::ReinitAtFork()
{
	printf("reinit_at_fork()\n");
	InitTeam();
	Thread::ReinitAtFork();
}


}


extern "C" {


status_t
_get_team_usage_info(team_id team, int32 who, team_usage_info* info, size_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
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

  	char procPath[B_PATH_NAME_LENGTH];
	char buffer[MAX_LINE_LENGTH];
	char commandProcPath[B_PATH_NAME_LENGTH];

	if (id == 0)
		id = getpid();

	sprintf(procPath, "/proc/%d/status", id);
	sprintf(commandProcPath, "/proc/%d/cmdline", id);
	FILE* fileid = fopen(procPath, "r");

	if (fileid == NULL)
		return B_BAD_VALUE;

	int i = 0;
	while (!feof(fileid)) {
		fgets(buffer, MAX_LINE_LENGTH, fileid);

		std::string temp(buffer);
		if (temp.find("Uid:\t")) {
			sscanf(buffer, "Uid:\t%u\t\n", &info->uid);
		} else if(temp.find("Gid:\t")) {
			sscanf(buffer, "Gid:\t%u\t\n", &info->gid);
		} else if (temp.find("Threads:\t") != std::string::npos) {
			sscanf(buffer, "Threads:\t%d\n", &info->thread_count);
			break;
		}
		i++;
	}

	int fileCommandId = open(commandProcPath, O_RDONLY);
	int bytesRead = read(fileCommandId, buffer, MAX_LINE_LENGTH);
	int argCount = 0;

	int j = 0;
	while (j < bytesRead) {
		if (buffer[j] == '\0') {
			argCount++;
			buffer[j] = ' ';
		}
		j++;
	}

	buffer[j] = '\0';
	info->argc = argCount;
	info->team = id;
	strncpy(info->args, buffer, MAX_BYTES);

	fclose(fileid);
	close(fileCommandId);

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
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_extended_team_info(team_id teamID, uint32 flags,
	void* buffer, size_t size, size_t* _sizeNeeded)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


}
