/*
 *  Copyright 2019, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <errno.h>

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
	int err = kill((pid_t) team, SIGKILL);
	if (err < 0)
		return B_BAD_TEAM_ID;

	return B_OK;
}


status_t
_kern_get_team_info(team_id id, team_info *info)
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
