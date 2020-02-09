/*
 *  Copyright 2018-2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#include "syscalls.h"

static std::list<int> gTeams;

#define UID_LINE 8
#define GID_LINE 9
#define MAX_BYTES 64
#define MAX_LINE_LENGTH 1024


status_t
_kern_get_team_usage_info(team_id team, int32 who, team_usage_info *info, size_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_kill_team(team_id team)
{
	int err = kill((pid_t) team, SIGKILL);
	if (err < 0)
		return B_BAD_TEAM_ID;

	return B_OK;
}


status_t
_kern_get_team_info(team_id id, team_info *info)
{
  	char procPath[B_PATH_NAME_LENGTH];
	char buffer[MAX_LINE_LENGTH];
	char commandProcPath[B_PATH_NAME_LENGTH];
    if ( id == 0 ) {
        id = getpid();        
	}

	sprintf(procPath, "/proc/%d/status", id);
	sprintf(commandProcPath, "/proc/%d/cmdline", id);
	FILE *fileid = fopen(procPath, "r");
    if( fileid == NULL ){
        return B_ERROR;
    }
	int fileCommandId = open(commandProcPath, O_RDONLY);
	int i=0;
	while( !feof(fileid) ){
		fgets(buffer, MAX_LINE_LENGTH, fileid);
		if(i == UID_LINE) {
			sscanf(buffer, "Uid:\t%u\t", &info->uid);
		} else if(i == GID_LINE) {
			sscanf(buffer, "Gid:\t%u\t", &info->gid);
		} else {
			if (strncmp(buffer, "Threads:", sizeof("Threads:")) == 0) {
				sscanf(buffer, "Threads:\t%d", &info->thread_count);
				break;
			}
		}
		i++;
	}
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
_kern_get_next_team_info(int32 *cookie, team_info *info)
{
	if (*cookie == 0) {
		DIR *procDir = opendir("/proc");
		struct dirent *dir;

		if (procDir != NULL) {
			while ((dir = readdir(procDir)) != NULL) {
				if (atoi(dir->d_name) > 0) {
					gTeams.push_front(atoi(dir->d_name));
				}
			}
		} else {
			return B_ERROR;
		}
		closedir(procDir);
	}
	if (*cookie > gTeams.size()) {
		return B_ERROR;
	}
	std::list <int> :: iterator gTeamsIt = gTeams.begin();
	std::advance(gTeamsIt, *cookie);
	unsigned int cur_pid = *gTeamsIt;
	(*cookie)++;

	return _kern_get_team_info(cur_pid, info);

}


status_t
_kern_get_memory_properties(team_id teamID, const void* address, uint32* _protected,
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
