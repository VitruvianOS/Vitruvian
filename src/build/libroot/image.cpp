/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <image.h>


status_t
_get_next_image_info(team_id team, int32* cookie,
	image_info* info, size_t infoSize)
{
	if (team < 0 || *cookie < 0 || info == NULL
			|| infoSize != sizeof(*info)) {
		return B_BAD_VALUE;
	}

	if (team == 0)
		team = getpid();

	if (*cookie == 0 && team == getpid()) {
		char path[B_PATH_NAME_LENGTH];
		sprintf(path, "/proc/%d/exe", team);

		ssize_t len = readlink(path, info->name, B_PATH_NAME_LENGTH - 1);
		if (len < 0)
			return B_ERROR;

		info->name[len] = '\0';
		// We use the team id to identify it's image
		// TODO: we probably want to use something else
		info->id = team;
		info->type = B_APP_IMAGE;
		info->sequence = 0;
		info->init_order = 0;

		// TODO: Fill remaining stuff
		*cookie+=1;
		return B_OK;
	}

	return B_ERROR;
}
