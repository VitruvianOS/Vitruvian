/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <Locker.h>

#include <errno.h>
#include <image.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string.h>
#include <unistd.h>

#include <map>

#include "Team.h"
#include "KernelDebug.h"
#include "MutexLock.h"


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


struct ImageIterState {
	int32		target;
	int32		current;
	image_info*	info;
	bool		found;
};


status_t
_get_image_info(image_id id, image_info* info, size_t infoSize)
{
	if (id < 0 || info == NULL || infoSize != sizeof(*info))
		return B_BAD_VALUE;

	int32 cookie = id - 1;
	return _get_next_image_info(B_CURRENT_TEAM, &cookie, info, infoSize);
}


status_t
_get_next_image_info(team_id team, int32* cookie,
	image_info* info, size_t infoSize)
{
	if (team < 0 || *cookie < 0 || info == NULL
			|| infoSize != sizeof(*info))
		return B_BAD_VALUE;

	if (team == 0)
		team = getpid();

	if (team != getpid())
		return B_NOT_SUPPORTED;

	ImageIterState state = {*cookie, 0, info, false};

	dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
			void* data) -> int {
		ImageIterState* state = (ImageIterState*)data;

		if (state->current != state->target) {
			state->current++;
			return 0;
		}

		if (phdr->dlpi_name == NULL || phdr->dlpi_name[0] == '\0') {
			ssize_t len = readlink("/proc/self/exe",
				state->info->name, B_PATH_NAME_LENGTH - 1);
			if (len < 0)
				state->info->name[0] = '\0';
			else
				state->info->name[len] = '\0';
			state->info->type = B_APP_IMAGE;
		} else {
			strlcpy(state->info->name, phdr->dlpi_name, B_PATH_NAME_LENGTH);
			state->info->type = B_LIBRARY_IMAGE;
		}

		state->info->id = state->current + 1;
		state->info->sequence = 0;
		state->info->init_order = 0;
		state->info->text = NULL;
		state->info->text_size = 0;
		state->info->data = NULL;
		state->info->data_size = 0;

		for (int i = 0; i < phdr->dlpi_phnum; i++) {
			const ElfW(Phdr)* ph = &phdr->dlpi_phdr[i];
			if (ph->p_type != PT_LOAD)
				continue;
			addr_t start = (addr_t)phdr->dlpi_addr + ph->p_vaddr;
			// PF_X executable
			if (ph->p_flags & 0x1) {
				state->info->text = (void*)start;
				state->info->text_size = ph->p_memsz;
			// PF_W writable
			} else if (ph->p_flags & 0x2) {
				state->info->data = (void*)start;
				state->info->data_size = ph->p_memsz;
			}
		}

		state->found = true;
		return 1;
	}, &state);

	if (!state.found)
		return B_ENTRY_NOT_FOUND;

	(*cookie)++;
	return B_OK;
}
