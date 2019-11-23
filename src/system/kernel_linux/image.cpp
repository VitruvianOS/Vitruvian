/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <errno.h>
#include <image.h>
#include <dlfcn.h>

#include "KernelDebug.h"

extern int gLoadImageFD = -1;

thread_id
load_image(int32 argc, const char** argv, const char** envp)
{
	TRACE("load_image: %s\n", argv[0]);
	for (int i = 0; i < argc; i++)
		printf("%s\n", argv[i]);
	int lockfd[2];
	if (pipe(lockfd) == -1)
		return -1;

	pid_t pid = fork();
	if (pid == 0) {
		close(lockfd[1]);

		// Wait for the father to unlock us
		uint32 value;
		read(lockfd[0], &value, sizeof(value));

		// TODO: const_cast for argv?
		int32 ret = execvpe(argv[0], argv, envp);
		if (ret == -1) {
			TRACE("execv err %s\n", strerror(errno));
			_exit(1);
			return -1;
		}
	} else {
		close(lockfd[0]);
		gLoadImageFD = lockfd[1];

		TRACE("load image ok\n");
		return pid;
	}
	return -1;
}


image_id
load_add_on(const char* path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	void* image = dlopen(path, RTLD_LAZY);
	if (image == NULL)
		return B_ERROR;

	return (image_id)image;
}


status_t
unload_add_on(image_id id)
{
	void* image = (void*)id;

	if (image == NULL)
		return B_BAD_VALUE;

	if (dlclose(image) != 0)
		return B_ERROR;

	return B_OK;
}


status_t
get_image_symbol(image_id id, const char* name,
	int32 sclass, void** pptr)
{
	void* image = (void*)id;

	if (image == NULL || name == NULL || pptr == NULL)
		return B_BAD_VALUE;

	void* sym = dlsym(image, name);
	if (sym == NULL)
		return B_ERROR;

	*pptr = sym;
	return B_OK;
}


status_t
_get_image_info(image_id id, image_info* info, size_t infoSize)
{
	if (id < 0 || info == NULL || infoSize != sizeof(*info))
		return B_BAD_VALUE;

	if (id == getpid()) {
		int32 cookie = 0;
		return _get_next_image_info(getpid(), 
			&cookie, info, sizeof(*info));
	}

	UNIMPLEMENTED();
	return B_ERROR;
}


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

	if (*cookie == 0) {
		char path[B_PATH_NAME_LENGTH];
		sprintf(path, "/proc/%d/exe", team);

		ssize_t len = readlink(path, info->name, B_PATH_NAME_LENGTH - 1);
		if (len < 0)
			return B_ERROR;

		info->name[len] = '\0';
		// We use the team id to identify it's image
		info->id = team;
		info->type = B_APP_IMAGE;
		info->sequence = 0;
		info->init_order = 0;

		// TODO: Fill remaining stuff
		*cookie+=1;
		return B_OK;
	}

	UNIMPLEMENTED();
	return B_ERROR;
}
