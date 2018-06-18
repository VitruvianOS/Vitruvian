/*
 ** Copyright 2004, Bill Hayden. All rights reserved
 ** Copyright 2018, Dario Casalinuovo. All rights reserved
 ** Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include "syscalls.h"


status_t
_kern_get_team_usage_info(team_id team, int32 who, team_usage_info *info, size_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_kill_team(team_id team)
{
	status_t ret = B_OK;
	int err;
	 
	err = kill((pid_t)team, SIGKILL);
	if (err < 0 && errno == ESRCH)
		ret = B_BAD_TEAM_ID;

	return ret;
}


status_t _kern_get_team_info(team_id id, team_info *info)
{		
	info->team = id;
	
	return B_OK;
}


status_t
_kern_get_next_team_info(int32 *cookie, team_info *info)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


int
atfork(void (*func)(void))
{
	return pthread_atfork(func, NULL, NULL);
}
