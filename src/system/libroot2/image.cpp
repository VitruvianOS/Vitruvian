/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <Locker.h>

#include <errno.h>
#include <image.h>
#include <dlfcn.h>

#include <map>

#include "Team.h"
#include "KernelDebug.h"
#include "MutexLock.h"


extern int gLoadImageFD = -1;


namespace BKernelPrivate {


class ImagePool {
public:
	static image_id	Load(const char* path) {
		if (path == NULL)
			return B_BAD_VALUE;

		MutexLocker _(&fLock);

		void* image = dlopen(path, RTLD_LAZY);

		if (image == NULL)
			return B_ERROR;

		fLoadedAddOns.insert(std::make_pair(++fId, image));

		return fId;
	}

	static status_t Unload(image_id id) {
		if (id <= 0)
			return B_BAD_VALUE;

		MutexLocker _(&fLock);

		void* image = _Find(id);
		if (image == NULL)
			return B_ERROR;

		if (dlclose(image) != 0)
			return B_ERROR;

		return B_OK;
	}

	static status_t FindSymbol(image_id id, const char* name,
		int32 sclass, void** pptr) {

		if (id < 0 || name == NULL || pptr == NULL)
			return B_BAD_VALUE;

		MutexLocker _(&fLock);

		void* image = _Find(id);
		if (image == NULL)
			return B_ERROR;

		void* symbol = dlsym(image, name);
		if (symbol == NULL)
			return B_ERROR;

		*pptr = symbol;

		return B_OK;
	}

private:
	static void* _Find(image_id id) {
		auto addon = fLoadedAddOns.find(id);

		if (addon == end(fLoadedAddOns))
			return NULL;

		return addon->second;
	}

	static std::map<image_id, void*> fLoadedAddOns;
	static image_id fId;
	static pthread_mutex_t fLock;
};


std::map<image_id, void*> ImagePool::fLoadedAddOns;
image_id ImagePool::fId = -1;
pthread_mutex_t ImagePool::fLock = PTHREAD_MUTEX_INITIALIZER;


}


thread_id
load_image(int32 argc, const char** argv, const char** envp)
{
	return BKernelPrivate::Team::LoadImage(argc, argv, envp);
}


image_id
load_add_on(const char* path)
{
	return BKernelPrivate::ImagePool::Load(path);
}


status_t
unload_add_on(image_id id)
{
	return BKernelPrivate::ImagePool::Unload(id);
}


status_t
get_image_symbol(image_id id, const char* name,
	int32 sclass, void** pptr)
{
	return BKernelPrivate::ImagePool::FindSymbol(id, name, sclass, pptr);
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
