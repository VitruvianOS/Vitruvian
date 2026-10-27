/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

// Registry state is never destroyed: the image API runs from static
// constructors and atexit handlers, outside libroot's own statics lifetime.
// Lock order: fRegistryLock, then fIdLock.

#include <errno.h>
#include <image.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "Team.h"
#include "KernelDebug.h"
#include "MutexLock.h"
#include "proc_read.h"

#include <new>

#include <libroot/libroot_private.h>


#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif
#ifndef STT_COMMON
#define STT_COMMON 5.8
#endif
#ifndef DT_GNU_HASH
#define DT_GNU_HASH 0x6ffffef5
#endif
#ifndef PF_X
#define PF_X 0x1
#endif
#ifndef PF_W
#define PF_W 0x2
#endif


namespace BKernelPrivate {


struct PhdrMatch {
	ElfW(Addr)	base;
	bool		found;
};


struct PhdrFill {
	ElfW(Addr)			base;
	const ElfW(Phdr)**	phdrs;
	uint16*				phnum;
	bool				found;
};


struct DynView {
	const ElfW(Sym)*	syms;
	const char*			str;
	uint32				count;
	ElfW(Addr)			base;
	const ElfW(Phdr)*	phdrs;
	uint16				phnum;
};


class ImageRegistry {
public:
	static image_id
	LoadAddOn(const char* path)
	{
		if (path == NULL)
			return B_BAD_VALUE;

		// One dlopen per load so unload can dlclose exactly once each.
		void* handle = dlopen(path, RTLD_LAZY);
		if (handle == NULL)
			return B_ERROR;

		struct link_map* lm = NULL;
		if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || lm == NULL) {
			dlclose(handle);
			return B_ERROR;
		}

		image_id id = _IdForBase(lm->l_addr);

		MutexLocker _(&fRegistryLock);
		auto it = _Loaded().find(id);
		if (it != _Loaded().end()) {
			it->second.refCount++;
			_CopyPath(it->second.path, path);
			return id;
		}

		LoadedAddOn entry;
		entry.handle = handle;
		entry.base = lm->l_addr;
		entry.refCount = 1;
		entry.isAddOn = true;
		entry.sequence = _NextSequenceLocked();
		_CopyPath(entry.path, path);
		_Loaded()[id] = entry;
		return id;
	}

	static status_t
	UnloadAddOn(image_id id)
	{
		if (id <= 0)
			return B_BAD_VALUE;

		void* handle = NULL;
		ElfW(Addr) base = 0;

		{
			MutexLocker _(&fRegistryLock);
			auto it = _Loaded().find(id);
			if (it == _Loaded().end())
				return B_BAD_IMAGE_ID;

			handle = it->second.handle;
			base = it->second.base;
			if (it->second.refCount > 1) {
				it->second.refCount--;
				// Pair with the extra dlopen from the matching Load.
				dlclose(handle);
				return B_OK;
			}
			_Loaded().erase(it);
		}

		if (dlclose(handle) != 0)
			return B_ERROR;

		// Ids follow the object: retire only once it is really gone.
		if (!_IsMapped(base)) {
			MutexLocker _(&fIdLock);
			_IdsByBase().erase(base);
			_SequencesByBase().erase(base);
		}
		return B_OK;
	}

	static status_t
	FindSymbol(image_id id, const char* name, int32 sclass, void** pptr)
	{
		if (id < 0 || name == NULL || pptr == NULL)
			return B_BAD_VALUE;

		void* handle = _HandleFor(id);
		if (handle == NULL)
			return B_ERROR;

		void* address = NULL;
		status_t error = _LookupSymbol(handle, name, sclass, &address);
		if (error != B_OK)
			return error;

		*pptr = address;
		return B_OK;
	}

	static status_t
	NthSymbol(image_id id, int32 n, char* nameBuffer, int32* nameLength,
		int32* sclass, void** location)
	{
		if (id < 0 || n < 0)
			return B_BAD_VALUE;

		void* handle = _HandleFor(id);
		if (handle == NULL)
			return B_ERROR;

		DynView view;
		if (!_DynViewFor(handle, &view))
			return B_ERROR;

		int32 index = 0;
		for (uint32 i = 0; i < view.count; i++) {
			const ElfW(Sym)* sym = &view.syms[i];
			if (sym->st_shndx == SHN_UNDEF || sym->st_name == 0)
				continue;

			int32 kind = _SymbolKind(sym, view);
			if (kind == 0)
				continue;

			if (index != n) {
				index++;
				continue;
			}

			const char* name = view.str + sym->st_name;
			int32 length = (int32)strlen(name);
			if (sclass != NULL)
				*sclass = kind;
			if (location != NULL)
				*location = (void*)(view.base + sym->st_value);
			if (nameBuffer != NULL) {
				int32 capacity = (nameLength != NULL && *nameLength > 0)
					? *nameLength : 0;
				int32 copy = length;
				if (capacity > 0) {
					if (copy >= capacity)
						copy = capacity - 1;
					memcpy(nameBuffer, name, copy);
					nameBuffer[copy] = '\0';
				}
			}
			if (nameLength != NULL)
				*nameLength = length;
			return B_OK;
		}

		return B_ENTRY_NOT_FOUND;
	}

	static status_t
	ImageInfo(image_id id, image_info* info, size_t infoSize)
	{
		if (id < 0 || info == NULL || infoSize != sizeof(*info))
			return B_BAD_VALUE;

		{
			MutexLocker _(&fRegistryLock);
			auto it = _Loaded().find(id);
			if (it != _Loaded().end()) {
				LoadedAddOn entry = it->second;
				// Fill outside the lock: it walks phdrs and dlsyms.
				if (_FillInfoFromHandle(id, entry.handle, entry.path,
						entry.isAddOn, entry.sequence, info) == B_OK)
					return B_OK;
			}

			// Another team's image, recorded when it was enumerated.
			auto remote = _RemoteByImage().find(id);
			if (remote != _RemoteByImage().end()) {
				memset(info, 0, sizeof(*info));
				info->id = id;
				_CopyPath(info->name, remote->second.c_str());
				info->type = _TypeForPath(info->name, false);
				return B_OK;
			}
		}

		// Borrowed and already-loaded objects (libbe, the app, ld.so).
		return _NextOwnImageFrom(0, id, info);
	}

	static status_t
	NextImageInfo(team_id team, int32* cookie, image_info* info,
		size_t infoSize)
	{
		if (team < 0 || cookie == NULL || *cookie < 0 || info == NULL
			|| infoSize != sizeof(*info))
			return B_BAD_VALUE;

		if (team == 0)
			team = getpid();

		if (team == getpid())
			return _NextOwnImage(cookie, info);

		return _NextRemoteImage(team, cookie, info);
	}

private:
	struct LoadedAddOn {
		void*		handle;
		ElfW(Addr)	base;
		int32		refCount;
		bool		isAddOn;
		int32		sequence;
		char		path[MAXPATHLEN];
	};

	struct RemoteImage {
		char		path[MAXPATHLEN];
		ElfW(Addr)	base;
		void*		text;
		int32		textSize;
		void*		data;
		int32		dataSize;
		dev_t		device;
		ino_t		node;
		image_id	id;
		image_type	type;
		int32		sequence;
	};

	static void
	_CopyPath(char* dest, const char* path)
	{
		dest[0] = '\0';
		if (path == NULL)
			return;
		size_t length = strlen(path);
		if (length >= MAXPATHLEN)
			length = MAXPATHLEN - 1;
		memcpy(dest, path, length);
		dest[length] = '\0';
	}

	// Ids follow the object, not its position in the link map: positions
	// shift when another object is unloaded, ids must not.
	static image_id
	_IdForBase(ElfW(Addr) base)
	{
		MutexLocker _(&fIdLock);
		auto it = _IdsByBase().find(base);
		if (it != _IdsByBase().end())
			return it->second;

		image_id id = fNextId++;
		_IdsByBase()[base] = id;
		return id;
	}

	static int32
	_NextSequenceLocked()
	{
		return fNextSequence++;
	}

	static int32
	_SequenceForBase(ElfW(Addr) base)
	{
		MutexLocker _(&fIdLock);
		auto it = _SequencesByBase().find(base);
		if (it != _SequencesByBase().end())
			return it->second;
		return 0;
	}

	static bool
	_IsMapped(ElfW(Addr) base)
	{
		PhdrMatch match = {base, false};
		dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
				void* data) -> int {
			PhdrMatch* m = (PhdrMatch*)data;
			if (phdr->dlpi_addr == m->base) {
				m->found = true;
				return 1;
			}
			return 0;
		}, &match);
		return match.found;
	}

	static void*
	_HandleFor(image_id id)
	{
		{
			MutexLocker _(&fRegistryLock);
			auto it = _Loaded().find(id);
			if (it != _Loaded().end())
				return it->second.handle;

			auto borrowed = _Borrowed().find(id);
			if (borrowed != _Borrowed().end())
				return borrowed->second;
		}

		return _BorrowHandle(id);
	}

	// Handles for already-loaded, non-add-on images. The walk takes the
	// registry lock itself, so it runs unlocked here.
	static void*
	_BorrowHandle(image_id id)
	{
		image_info info;
		if (_NextOwnImageFrom(0, id, &info) != B_OK)
			return NULL;

		void* handle = NULL;
		if (info.type == B_APP_IMAGE) {
			// The main program. MUST use dlopen(NULL): dlopen'ing the exe
			// by path would load it as a library and hit DF_1_PIE.
			handle = dlopen(NULL, RTLD_LAZY);
		} else if (info.name[0] != '\0') {
			handle = dlopen(info.name, RTLD_NOLOAD | RTLD_LAZY);
		}
		if (handle == NULL)
			return NULL;

		MutexLocker _(&fRegistryLock);
		auto it = _Borrowed().find(id);
		if (it != _Borrowed().end()) {
			// Another thread borrowed it meanwhile; keep one reference.
			dlclose(handle);
			return it->second;
		}
		_Borrowed()[id] = handle;
		return handle;
	}

	static bool
	_DynViewFor(void* handle, DynView* view)
	{
		struct link_map* lm = NULL;
		if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || lm == NULL
			|| lm->l_ld == NULL)
			return false;

		memset(view, 0, sizeof(*view));
		view->base = lm->l_addr;

		const ElfW(Sym)* syms = NULL;
		const char* str = NULL;
		const void* hash = NULL;
		const void* gnuHash = NULL;

		// glibc relocates these entries in place, except for objects it
		// does not relocate (the vDSO): there they are still offsets.
		auto address = [lm](ElfW(Addr) ptr) -> ElfW(Addr) {
			return ptr < lm->l_addr ? ptr + lm->l_addr : ptr;
		};

		for (const ElfW(Dyn)* d = lm->l_ld; d->d_tag != DT_NULL; d++) {
			switch (d->d_tag) {
				case DT_SYMTAB:
					syms = (const ElfW(Sym)*)address(d->d_un.d_ptr);
					break;
				case DT_STRTAB:
					str = (const char*)address(d->d_un.d_ptr);
					break;
				case DT_HASH:
					hash = (const void*)address(d->d_un.d_ptr);
					break;
				case DT_GNU_HASH:
					gnuHash = (const void*)address(d->d_un.d_ptr);
					break;
				default:
					break;
			}
		}

		if (syms == NULL || str == NULL)
			return false;

		view->syms = syms;
		view->str = str;
		view->count = _SymbolCount(hash, gnuHash);

		view->phdrs = NULL;
		view->phnum = 0;
		PhdrFill fill;
		fill.base = view->base;
		fill.phdrs = &view->phdrs;
		fill.phnum = &view->phnum;
		fill.found = false;
		dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
				void* data) -> int {
			PhdrFill* f = (PhdrFill*)data;
			if (phdr->dlpi_addr == f->base) {
				*f->phdrs = phdr->dlpi_phdr;
				*f->phnum = phdr->dlpi_phnum;
				f->found = true;
				return 1;
			}
			return 0;
		}, &fill);

		return view->count > 0;
	}

	static uint32
	_SymbolCount(const void* hash, const void* gnuHash)
	{
		if (hash != NULL) {
			const uint32* h = (const uint32*)hash;
			return h[1];
		}
		if (gnuHash != NULL) {
			const uint32* h = (const uint32*)gnuHash;
			uint32 nbuckets = h[0];
			uint32 symoffset = h[1];
			uint32 bloomSize = h[2];
			const uint32* buckets = h + 4
				+ bloomSize * (sizeof(ElfW(Addr)) / sizeof(uint32));
			const uint32* chain = buckets + nbuckets;
			uint32 max = symoffset;
			for (uint32 i = 0; i < nbuckets; i++) {
				if (buckets[i] > max)
					max = buckets[i];
			}
			while ((chain[max - symoffset] & 1) == 0)
				max++;
			return max + 1;
		}
		return 0;
	}

	static int32
	_SymbolKind(const ElfW(Sym)* sym, const DynView& view)
	{
		int type = ELF32_ST_TYPE(sym->st_info);
		if (type == STT_FUNC || type == STT_GNU_IFUNC)
			return B_SYMBOL_TYPE_TEXT;
		if (type == STT_OBJECT || type == STT_COMMON)
			return B_SYMBOL_TYPE_DATA;
		if (type != STT_NOTYPE || view.phdrs == NULL)
			return 0;

		// Untyped symbols: classify by which PT_LOAD they sit in.
		addr_t address = (addr_t)(view.base + sym->st_value);
		for (uint16 i = 0; i < view.phnum; i++) {
			const ElfW(Phdr)* ph = &view.phdrs[i];
			if (ph->p_type != PT_LOAD)
				continue;
			addr_t start = (addr_t)view.base + ph->p_vaddr;
			if (address < start || address >= start + ph->p_memsz)
				continue;
			if (ph->p_flags & PF_X)
				return B_SYMBOL_TYPE_TEXT;
			if (ph->p_flags & PF_W)
				return B_SYMBOL_TYPE_DATA;
		}
		return B_SYMBOL_TYPE_TEXT;
	}

	static bool
	_KindMatches(int32 symbolKind, int32 sclass)
	{
		if (sclass == B_SYMBOL_TYPE_ANY)
			return true;
		return symbolKind == sclass;
	}

	static status_t
	_LookupSymbol(void* handle, const char* name, int32 sclass, void** out)
	{
		DynView view;
		if (_DynViewFor(handle, &view)) {
			for (uint32 i = 0; i < view.count; i++) {
				const ElfW(Sym)* sym = &view.syms[i];
				if (sym->st_shndx == SHN_UNDEF || sym->st_name == 0)
					continue;
				if (strcmp(view.str + sym->st_name, name) != 0)
					continue;
				int32 kind = _SymbolKind(sym, view);
				if (!_KindMatches(kind, sclass))
					continue;
				*out = (void*)(view.base + sym->st_value);
				return B_OK;
			}
			return B_MISSING_SYMBOL;
		}

		// No usable dynamic symbol table: fall back to dlsym, which is
		// class-agnostic, and accept the result only for B_SYMBOL_TYPE_ANY.
		if (sclass != B_SYMBOL_TYPE_ANY)
			return B_MISSING_SYMBOL;

		void* symbol = dlsym(handle, name);
		if (symbol == NULL)
			return B_MISSING_SYMBOL;
		*out = symbol;
		return B_OK;
	}

	static void
	_FillLoadPhdrs(const ElfW(Phdr)* phdrs, uint16 phnum, ElfW(Addr) base,
		image_info* info)
	{
		info->text = NULL;
		info->text_size = 0;
		info->data = NULL;
		info->data_size = 0;

		// Libraries usually have a read-only, an executable and a writable
		// PT_LOAD; take the first of each class, not the last seen.
		for (uint16 i = 0; i < phnum; i++) {
			const ElfW(Phdr)* ph = &phdrs[i];
			if (ph->p_type != PT_LOAD)
				continue;
			addr_t start = (addr_t)base + ph->p_vaddr;
			if ((ph->p_flags & PF_X) && info->text == NULL) {
				info->text = (void*)start;
				info->text_size = (int32)ph->p_memsz;
			} else if ((ph->p_flags & PF_W) && info->data == NULL) {
				info->data = (void*)start;
				info->data_size = (int32)ph->p_memsz;
			}
		}
	}

	static void
	_FillStat(const char* path, image_info* info)
	{
		struct stat st;
		info->device = 0;
		info->node = 0;
		if (path == NULL || path[0] == '\0')
			return;
		if (stat(path, &st) == 0) {
			info->device = st.st_dev;
			info->node = st.st_ino;
		}
	}

	static image_type
	_TypeForPath(const char* path, bool isAddOn)
	{
		if (isAddOn)
			return B_ADD_ON_IMAGE;
		if (path == NULL || path[0] == '\0')
			return B_APP_IMAGE;
		if (strstr(path, "ld-linux") != NULL || strstr(path, "/ld-") != NULL
			|| strcmp(path, "linux-vdso.so.1") == 0)
			return B_SYSTEM_IMAGE;
		return B_LIBRARY_IMAGE;
	}

	static void
	_FillRoutines(const char* name, void* handle, image_info* info)
	{
		info->init_routine = NULL;
		info->term_routine = NULL;
		if (handle == NULL || name == NULL || name[0] == '\0')
			return;

		void* init = dlsym(handle, B_INIT_BEFORE_FUNCTION_NAME);
		void* term = dlsym(handle, B_TERM_BEFORE_FUNCTION_NAME);
		info->init_routine = (void (*)())init;
		info->term_routine = (void (*)())term;
	}

	static status_t
	_FillInfoFromHandle(image_id id, void* handle, const char* path,
		bool isAddOn, int32 sequence, image_info* info)
	{
		struct link_map* lm = NULL;
		if (handle != NULL
			&& dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0)
			lm = NULL;

		ElfW(Addr) base = 0;
		if (lm != NULL)
			base = lm->l_addr;

		memset(info, 0, sizeof(*info));
		info->id = id;
		info->type = _TypeForPath(path, isAddOn);
		info->sequence = sequence;
		info->init_order = isAddOn ? sequence : 0;
		_CopyPath(info->name, path != NULL ? path : "");
		if (lm != NULL && lm->l_name != NULL && lm->l_name[0] != '\0'
			&& info->name[0] == '\0')
			_CopyPath(info->name, lm->l_name);

		if (info->type == B_APP_IMAGE && info->name[0] == '\0') {
			ssize_t len = readlink("/proc/self/exe", info->name,
				MAXPATHLEN - 1);
			if (len < 0)
				info->name[0] = '\0';
			else
				info->name[len] = '\0';
		}

		if (lm != NULL && lm->l_ld != NULL) {
			const ElfW(Phdr)* phdrs = NULL;
			uint16 phnum = 0;
			PhdrFill fill;
			fill.base = base;
			fill.phdrs = &phdrs;
			fill.phnum = &phnum;
			fill.found = false;
			dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
					void* data) -> int {
				PhdrFill* f = (PhdrFill*)data;
				if (phdr->dlpi_addr == f->base) {
					*f->phdrs = phdr->dlpi_phdr;
					*f->phnum = phdr->dlpi_phnum;
					f->found = true;
					return 1;
				}
				return 0;
			}, &fill);
			_FillLoadPhdrs(phdrs, phnum, base, info);
		}

		_FillStat(info->name, info);
		_FillRoutines(info->name, handle, info);
		return B_OK;
	}

	static status_t
	_NextOwnImage(int32* cookie, image_info* info)
	{
		// No registry locks here: dl_iterate_phdr holds the linker lock,
		// re-entering the registry deadlocks against Load/Unload.
		struct Pick {
			int32		target;
			int32		current;
			ElfW(Addr)	base;
			const ElfW(Phdr)*	phdrs;
			uint16				phnum;
			char				name[MAXPATHLEN];
			image_type			type;
			bool				found;
		};

		Pick pick;
		memset(&pick, 0, sizeof(pick));
		pick.target = *cookie;
		pick.type = B_LIBRARY_IMAGE;

		dl_iterate_phdr([](struct dl_phdr_info* phdr, size_t size,
				void* data) -> int {
			Pick* pick = (Pick*)data;
			if (pick->current != pick->target) {
				pick->current++;
				return 0;
			}

			pick->base = phdr->dlpi_addr;
			pick->phdrs = phdr->dlpi_phdr;
			pick->phnum = phdr->dlpi_phnum;
			if (phdr->dlpi_name == NULL || phdr->dlpi_name[0] == '\0') {
				ssize_t len = readlink("/proc/self/exe", pick->name,
					MAXPATHLEN - 1);
				if (len < 0)
					pick->name[0] = '\0';
				else
					pick->name[len] = '\0';
				pick->type = B_APP_IMAGE;
			} else {
				_CopyPath(pick->name, phdr->dlpi_name);
				pick->type = B_LIBRARY_IMAGE;
			}
			pick->found = true;
			return 1;
		}, &pick);

		if (!pick.found)
			return B_ENTRY_NOT_FOUND;

		image_id id = 0;
		bool isAddOn = false;
		int32 sequence = 0;
		{
			MutexLocker _(&fRegistryLock);
			id = _IdForBaseUnlocked(pick.base);
			auto it = _Loaded().find(id);
			if (it != _Loaded().end() && it->second.isAddOn)
				isAddOn = true;
			MutexLocker __(&fIdLock);
			sequence = _SequenceForBaseLocked(pick.base);
		}

		if (isAddOn)
			pick.type = B_ADD_ON_IMAGE;
		else if (pick.type != B_APP_IMAGE)
			pick.type = _TypeForPath(pick.name, false);

		memset(info, 0, sizeof(*info));
		info->id = id;
		info->type = pick.type;
		info->sequence = sequence;
		info->init_order = isAddOn ? sequence : 0;
		_CopyPath(info->name, pick.name);
		_FillLoadPhdrs(pick.phdrs, pick.phnum, pick.base, info);
		_FillStat(info->name, info);

		(*cookie)++;
		return B_OK;
	}

	// Used by ImageInfo before the registry entry is consulted; the caller
	// holds fRegistryLock, so the id map lock is still available.
	static image_id
	_IdForBaseUnlocked(ElfW(Addr) base)
	{
		MutexLocker _(&fIdLock);
		auto it = _IdsByBase().find(base);
		if (it != _IdsByBase().end())
			return it->second;
		image_id id = fNextId++;
		_IdsByBase()[base] = id;
		return id;
	}

	static int32
	_SequenceForBaseLocked(ElfW(Addr) base)
	{
		auto it = _SequencesByBase().find(base);
		if (it != _SequencesByBase().end())
			return it->second;
		return 0;
	}

	static status_t
	_NextOwnImageFrom(int32 startCookie, image_id id, image_info* info)
	{
		int32 cookie = startCookie;
		image_info candidate;
		while (_NextOwnImage(&cookie, &candidate) == B_OK) {
			if (candidate.id == id) {
				*info = candidate;
				return B_OK;
			}
		}
		return B_BAD_IMAGE_ID;
	}

	static image_id
	_RemoteId(team_id team, const char* path, ElfW(Addr) base)
	{
		// Bit 30 keeps these apart from local ids; image_id is signed.
		uint32 hash = 2166136261u;
		for (const unsigned char* p = (const unsigned char*)path;
				p != NULL && *p != '\0'; p++) {
			hash ^= *p;
			hash *= 16777619u;
		}
		hash ^= (uint32)base;
		hash ^= (uint32)(base >> 32);
		hash ^= (uint32)team;
		hash = (hash & 0x3fffffffu) | 0x40000000u;
		if ((image_id)hash <= 0)
			hash = 0x40000001u;
		return (image_id)hash;
	}

	static bool
	_ParseMapsLine(const char* line, RemoteImage* out)
	{
		struct proc_map_line map;
		if (!proc_parse_map_header(line, &map))
			return false;

		// Only file-backed mappings.
		if (map.pathLength == 0 || map.path[0] != '/')
			return false;

		out->base = (ElfW(Addr))(map.start - map.offset);
		_CopyPath(out->path, map.path);
		out->text = NULL;
		out->textSize = 0;
		out->data = NULL;
		out->dataSize = 0;
		out->device = makedev(map.devMajor, map.devMinor);
		out->node = (ino_t)map.inode;
		out->type = _TypeForPath(out->path, false);

		if (map.perms[1] == 'x') {
			out->text = (void*)map.start;
			out->textSize = (int32)(map.end - map.start);
		}
		if (map.perms[1] == 'w') {
			out->data = (void*)map.start;
			out->dataSize = (int32)(map.end - map.start);
		}
		return true;
	}

	static void
	_ElfPhdrsFromFile(const char* path, ElfW(Addr) base, image_info* info)
	{
		// Never block here: callers enumerate other teams' mappings.
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
		if (fd < 0)
			return;
		struct stat st;
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
			close(fd);
			return;
		}

		ElfW(Ehdr) ehdr;
		if (pread(fd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr)
			|| memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
			close(fd);
			return;
		}

		std::vector<ElfW(Phdr)> phdrs(ehdr.e_phnum);
		if (pread(fd, phdrs.data(), sizeof(ElfW(Phdr)) * ehdr.e_phnum,
				ehdr.e_phoff)
				!= (ssize_t)(sizeof(ElfW(Phdr)) * ehdr.e_phnum)) {
			close(fd);
			return;
		}
		close(fd);

		_FillLoadPhdrs(phdrs.data(), ehdr.e_phnum, base, info);
	}

	// One team's file-backed images, parsed once per enumeration.
	struct RemoteImageScan {
		team_id						team;
		size_t						next;
		std::vector<RemoteImage>	images;
	};

	// Cap on the maps slurp; over the cap the enumeration fails instead of
	// growing an unbounded allocation.
	static const size_t kMaxMapsBytes = 8 * 1024 * 1024;


	static status_t
	_BuildRemoteImages(team_id team, std::vector<RemoteImage>& images)
	{
		char mapsPath[64];
		snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", (int)team);

		int fd = open(mapsPath, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			if (errno == EACCES || errno == EPERM)
				return B_PERMISSION_DENIED;
			if (errno == ENOENT)
				return B_BAD_TEAM_ID;
			return B_ERROR;
		}

		// Read once: the seq_file regenerates when seeked, which duplicates
		// or skips records across successive calls. Capped allocation.
		char* buffer = NULL;
		size_t used = 0;
		if (!proc_slurp(fd, &buffer, &used, kMaxMapsBytes)) {
			int error = errno;
			close(fd);
			return error == EOVERFLOW ? B_NO_MEMORY : B_ERROR;
		}
		close(fd);

		std::vector<char*> lines;
		char* cursor = buffer;
		char* end = buffer + used;
		while (cursor < end) {
			char* nl = (char*)memchr(cursor, '\n', end - cursor);
			if (nl == NULL) {
				lines.push_back(cursor);
				break;
			}
			*nl = '\0';
			lines.push_back(cursor);
			cursor = nl + 1;
		}

		for (size_t i = 0; i < lines.size(); i++) {
			RemoteImage image;
			memset(&image, 0, sizeof(image));
			if (!_ParseMapsLine(lines[i], &image))
				continue;

			// Only regular files can be images. Mapped devices (an ALSA PCM
			// buffer, a GPU node) are not, and opening a busy one blocks.
			struct stat st;
			if (stat(image.path, &st) != 0 || !S_ISREG(st.st_mode))
				continue;

			bool known = false;
			for (size_t j = 0; j < images.size(); j++) {
				if (strcmp(images[j].path, image.path) == 0) {
					known = true;
					// Keep the lowest base; merge text/data from all maps.
					if (image.base < images[j].base)
						images[j].base = image.base;
					if (image.text != NULL && images[j].text == NULL) {
						images[j].text = image.text;
						images[j].textSize = image.textSize;
					}
					if (image.data != NULL && images[j].data == NULL) {
						images[j].data = image.data;
						images[j].dataSize = image.dataSize;
					}
					break;
				}
			}
			if (!known)
				images.push_back(image);
		}

		free(buffer);
		return B_OK;
	}


	static status_t
	_NextRemoteImage(team_id team, int32* cookie, image_info* info)
	{
		// A dead handle keeps reporting B_ENTRY_NOT_FOUND, so a retry with
		// the same cookie cannot restart the walk.
		RemoteImageScan* scan = (RemoteImageScan*)proc_enum_state(*cookie);
		if (scan == NULL) {
			if (*cookie != 0)
				return B_ENTRY_NOT_FOUND;

			scan = new (std::nothrow) RemoteImageScan();
			if (scan == NULL)
				return B_NO_MEMORY;

			scan->team = team;
			scan->next = 0;

			status_t error = _BuildRemoteImages(team, scan->images);
			if (error != B_OK) {
				delete scan;
				return error;
			}

			*cookie = proc_enum_alloc(scan);
			if (*cookie == 0) {
				delete scan;
				return B_NO_MEMORY;
			}
		}

		if (scan->next >= scan->images.size()) {
			delete (RemoteImageScan*)proc_enum_release(*cookie);
			return B_ENTRY_NOT_FOUND;
		}

		RemoteImage& image = scan->images[scan->next];
		memset(info, 0, sizeof(*info));
		info->id = _RemoteId(team, image.path, image.base);
		info->type = image.type;
		info->sequence = (int32)scan->next;
		info->init_order = 0;
		_CopyPath(info->name, image.path);
		_ElfPhdrsFromFile(image.path, image.base, info);
		if (info->text == NULL && image.text != NULL) {
			info->text = image.text;
			info->text_size = image.textSize;
		}
		if (info->data == NULL && image.data != NULL) {
			info->data = image.data;
			info->data_size = image.dataSize;
		}
		_FillStat(image.path, info);

		{
			MutexLocker _(&fRegistryLock);
			_RemoteByImage()[info->id] = info->name;
		}

		scan->next++;
		return B_OK;
	}

	// Built on first use and never destroyed: image calls arrive during other
	// libraries' static initialisation and from destructors at exit.
	static std::map<image_id, LoadedAddOn>& _Loaded()
	{
		static std::map<image_id, LoadedAddOn>* sMap
			= new std::map<image_id, LoadedAddOn>;
		return *sMap;
	}

	static std::map<image_id, void*>& _Borrowed()
	{
		static std::map<image_id, void*>* sMap
			= new std::map<image_id, void*>;
		return *sMap;
	}

	static std::map<ElfW(Addr), image_id>& _IdsByBase()
	{
		static std::map<ElfW(Addr), image_id>* sMap
			= new std::map<ElfW(Addr), image_id>;
		return *sMap;
	}

	static std::map<ElfW(Addr), int32>& _SequencesByBase()
	{
		static std::map<ElfW(Addr), int32>* sMap
			= new std::map<ElfW(Addr), int32>;
		return *sMap;
	}

	static std::map<image_id, std::string>& _RemoteByImage()
	{
		static std::map<image_id, std::string>* sMap
			= new std::map<image_id, std::string>;
		return *sMap;
	}

	static pthread_mutex_t fRegistryLock;
	static image_id fNextId;
	static int32 fNextSequence;
	static pthread_mutex_t fIdLock;
};


pthread_mutex_t ImageRegistry::fRegistryLock = PTHREAD_MUTEX_INITIALIZER;
image_id ImageRegistry::fNextId = 1;
int32 ImageRegistry::fNextSequence = 1;
pthread_mutex_t ImageRegistry::fIdLock = PTHREAD_MUTEX_INITIALIZER;


}


thread_id
load_image(int32 argc, const char** argv, const char** envp)
{
	return BKernelPrivate::Team::LoadImage(argc, argv, envp);
}


// Like load_image(), but hands back the exec status pipe's read end. A
// successful exec closes it (CLOEXEC); the child reports a failed exec as a
// negative status on it. Consume with __wait_exec_status() after resuming
// the thread.
thread_id
__load_image_with_status(int32 argc, const char** argv, const char** envp,
	int* execStatusFd)
{
	return BKernelPrivate::Team::LoadImage(argc, argv, envp, execStatusFd);
}


image_id
load_add_on(const char* path)
{
	return BKernelPrivate::ImageRegistry::LoadAddOn(path);
}


status_t
unload_add_on(image_id id)
{
	return BKernelPrivate::ImageRegistry::UnloadAddOn(id);
}


status_t
get_image_symbol(image_id id, const char* name,
	int32 sclass, void** pptr)
{
	return BKernelPrivate::ImageRegistry::FindSymbol(id, name, sclass,
		pptr);
}


status_t
get_nth_image_symbol(image_id id, int32 n, char* nameBuffer,
	int32* nameLength, int32* sclass, void** location)
{
	return BKernelPrivate::ImageRegistry::NthSymbol(id, n, nameBuffer,
		nameLength, sclass, location);
}


status_t
_get_image_info(image_id id, image_info* info, size_t infoSize)
{
	return BKernelPrivate::ImageRegistry::ImageInfo(id, info, infoSize);
}


status_t
_get_next_image_info(team_id team, int32* cookie,
	image_info* info, size_t infoSize)
{
	return BKernelPrivate::ImageRegistry::NextImageInfo(team, cookie,
		info, infoSize);
}
