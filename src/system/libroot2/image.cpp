/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
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


struct FindByBaseState {
	ElfW(Addr)	base;
	int32		index;
	int32		current;
};


class ImagePool {
public:
	static image_id	Load(const char* path) {
		if (path == NULL)
			return B_BAD_VALUE;

		void* handle = dlopen(path, RTLD_LAZY);
		if (handle == NULL)
			return B_ERROR;

		struct link_map* lm = NULL;
		if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || lm == NULL) {
			dlclose(handle);
			return B_ERROR;
		}

		image_id id = IdForBase(lm->l_addr);

		MutexLocker _(&fLock);
		_LoadedAddOns()[id] = LoadedAddOn{handle, lm->l_addr};
		return id;
	}

	// Ids follow the object, not its position in the link map: positions
	// shift when another object is unloaded, ids must not.
	static image_id IdForBase(ElfW(Addr) base) {
		MutexLocker _(&fIdLock);
		auto it = _IdsByBase().find(base);
		if (it != _IdsByBase().end())
			return it->second;
		image_id id = fNextId++;
		_IdsByBase()[base] = id;
		return id;
	}

	static status_t Unload(image_id id) {
		if (id <= 0)
			return B_BAD_VALUE;

		MutexLocker _(&fLock);

		auto it = _LoadedAddOns().find(id);
		if (it == _LoadedAddOns().end())
			return B_ERROR;

		ElfW(Addr) base = it->second.base;
		if (dlclose(it->second.handle) != 0)
			return B_ERROR;

		_LoadedAddOns().erase(it);

		// Retire the id once the object is gone, so an object loaded at
		// the same address later gets a new one.
		if (!_IsMapped(base)) {
			MutexLocker _(&fIdLock);
			_IdsByBase().erase(base);
		}
		return B_OK;
	}

	static status_t FindSymbol(image_id id, const char* name,
		int32 sclass, void** pptr) {

		if (id < 0 || name == NULL || pptr == NULL)
			return B_BAD_VALUE;

		MutexLocker _(&fLock);

		void* handle = _Find(id);
		if (handle == NULL)
			handle = _BorrowHandle(id);
		if (handle == NULL)
			return B_ERROR;

		void* symbol = dlsym(handle, name);
		if (symbol == NULL)
			return B_ERROR;

		*pptr = symbol;
		return B_OK;
	}

private:
	static void* _Find(image_id id) {
		auto it = _LoadedAddOns().find(id);
		if (it == _LoadedAddOns().end())
			return NULL;
		return it->second.handle;
	}

	static void* _BorrowHandle(image_id id) {
		auto it = _Borrowed().find(id);
		if (it != _Borrowed().end())
			return it->second;

		image_info info;
		if (::_get_image_info(id, &info, sizeof(info)) != B_OK)
			return NULL;

		void* handle = NULL;
		if (info.type == B_APP_IMAGE) {
			// Main program. MUST use dlopen(NULL) — dlopen'ing the exe by path
			// would try to load it as a library and hit glibc's DF_1_PIE wall.
			handle = dlopen(NULL, RTLD_LAZY);
		} else if (info.name[0] != '\0') {
			// Already-loaded shared object (libbe, ...). RTLD_NOLOAD returns
			// the existing handle without reloading.
			handle = dlopen(info.name, RTLD_NOLOAD | RTLD_LAZY);
		}
		if (handle == NULL)
			return NULL;

		_Borrowed()[id] = handle;
		return handle;
	}

	static bool _IsMapped(ElfW(Addr) base) {
		FindByBaseState state = {base, -1, 0};
		dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
				void* data) -> int {
			FindByBaseState* s = (FindByBaseState*)data;
			if (phdr->dlpi_addr == s->base) {
				s->index = s->current;
				return 1;
			}
			s->current++;
			return 0;
		}, &state);
		return state.index >= 0;
	}

	struct LoadedAddOn {
		void*		handle;
		ElfW(Addr)	base;
	};

	// Built on first use and never destroyed: image calls arrive during other
	// libraries' static initialisation and from destructors at exit.
	static std::map<image_id, LoadedAddOn>& _LoadedAddOns() {
		static std::map<image_id, LoadedAddOn>* sMap
			= new std::map<image_id, LoadedAddOn>;
		return *sMap;
	}

	// Handles for already-loaded, non-add-on images
	static std::map<image_id, void*>& _Borrowed() {
		static std::map<image_id, void*>* sMap
			= new std::map<image_id, void*>;
		return *sMap;
	}

	static pthread_mutex_t fLock;

	// Taken after fLock when both are needed, never inside dl_iterate_phdr.
	static std::map<ElfW(Addr), image_id>& _IdsByBase() {
		static std::map<ElfW(Addr), image_id>* sMap
			= new std::map<ElfW(Addr), image_id>;
		return *sMap;
	}

	static image_id fNextId;
	static pthread_mutex_t fIdLock;
};


pthread_mutex_t ImagePool::fLock = PTHREAD_MUTEX_INITIALIZER;
image_id ImagePool::fNextId = 1;
pthread_mutex_t ImagePool::fIdLock = PTHREAD_MUTEX_INITIALIZER;


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
	ElfW(Addr)	base;
};


status_t
_get_image_info(image_id id, image_info* info, size_t infoSize)
{
	if (id < 0 || info == NULL || infoSize != sizeof(*info))
		return B_BAD_VALUE;

	int32 cookie = 0;
	while (_get_next_image_info(B_CURRENT_TEAM, &cookie, info, infoSize)
			== B_OK) {
		if (info->id == id)
			return B_OK;
	}
	return B_BAD_IMAGE_ID;
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

	ImageIterState state = {*cookie, 0, info, false, 0};

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

		state->base = phdr->dlpi_addr;
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

	info->id = BKernelPrivate::ImagePool::IdForBase(state.base);
	(*cookie)++;
	return B_OK;
}
