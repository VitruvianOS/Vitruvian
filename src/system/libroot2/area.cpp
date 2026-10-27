/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

/* cc1plus predefines _GNU_SOURCE for C++. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <KernelExport.h>
#include <OS.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <map>
#include <new>
#include <string>

#include "MutexLock.h"
#include "../kernel/nexus/nexus/nexus.h"
#include "Team.h"
#include "proc_read.h"


namespace BKernelPrivate {


struct LocalArea {
	area_id     id;

	std::string name;
	void*       address;
	size_t      size;
	uint32_t    protection;
	int         memfd;
};


class AreaPool {
public:

	static AreaPool& Get() {
		static AreaPool instance;
		return instance;
	}

	void Add(const LocalArea& area) {
		MutexLocker _(&fLock);

		fAreasMap[area.id] = area;
	}

	bool Get(area_id id, LocalArea& out) {
		MutexLocker _(&fLock);

		auto it = fAreasMap.find(id);
		if (it == fAreasMap.end())
			return false;

		out = it->second;
		return true;
	}

	bool Remove(area_id id, LocalArea& out) {
		MutexLocker _(&fLock);

		auto it = fAreasMap.find(id);
		if (it == fAreasMap.end())
			return false;

		out = it->second;
		fAreasMap.erase(it);
		return true;
	}

	void Update(area_id id, void* address, size_t size) {
		MutexLocker _(&fLock);

		auto it = fAreasMap.find(id);
		if (it != fAreasMap.end()) {
			it->second.address = address;
			it->second.size = size;
		}
	}

	area_id FindByAddress(void* address) {
		MutexLocker _(&fLock);

		for (const auto& p : fAreasMap) {
			uintptr_t start = (uintptr_t)p.second.address;
			uintptr_t end = start + p.second.size;
			if ((uintptr_t)address >= start && (uintptr_t)address < end)
				return p.first;

		}
		return B_ERROR;
	}

	area_id FindByName(const char* name) {
		MutexLocker _(&fLock);

		for (const auto& p : fAreasMap) {
			if (p.second.name == name)
				return p.first;
		}
		return B_NAME_NOT_FOUND;
	}

private:
	AreaPool() {
		pthread_mutex_init(&fLock, NULL);
	}

	~AreaPool() {
		pthread_mutex_destroy(&fLock);
	}

	std::map<area_id, LocalArea>	fAreasMap;
	pthread_mutex_t 				fLock;
};


// smaps is a seq_file: seeking backward in it can force the kernel to
// regenerate the output from the start, so slurp it once and walk an
// in-memory offset instead of holding a FILE* across calls.
enum AreaScanMode {
	kAreaScanSmaps,	// per-mapping data, same-uid teams only
	kAreaScanStatm	// whole-team totals synthesized into 2 records, any team
};


struct AreaScanState {
	AreaScanMode	mode;
	team_id			team;

	// kAreaScanSmaps
	char*			buffer;
	size_t			length;
	size_t			offset;

	// kAreaScanStatm: precomputed at open time, handed out one per call.
	uint64			privateBytes;
	uint64			sharedBytes;
	int				statmNext;	// 0 = private pending, 1 = shared pending, 2 = done
};


static int protection_to_prot(uint32_t prot)
{
	int p = 0;
	if (prot & B_READ_AREA) p |= PROT_READ;
	if (prot & B_WRITE_AREA) p |= PROT_WRITE;
	if (prot & B_EXECUTE_AREA) p |= PROT_EXEC;
	if (prot & B_KERNEL_READ_AREA) p |= PROT_READ;
	if (prot & B_KERNEL_WRITE_AREA) p |= PROT_WRITE;
	return p ? p : PROT_READ;
}


}  // namespace BKernelPrivate


extern "C" {


area_id
create_area(const char* name, void** startAddr, uint32 addrSpec,
	size_t size, uint32 lock, uint32 protection)
{
	if (name == NULL || name[0] == '\0' || size == 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	// TODO if pageSize != B_PAGE_SIZE emit warning
	// See also #130
	size_t pageSize = sysconf(_SC_PAGESIZE);
	size = (size + pageSize - 1) & ~(pageSize - 1);

	int memfd = syscall(SYS_memfd_create, name, 0);
	if (memfd < 0)
		return B_NO_MEMORY;

	if (ftruncate(memfd, size) < 0) {
		close(memfd);
		return B_NO_MEMORY;
	}

	int prot = BKernelPrivate::protection_to_prot(protection);
	int flags = MAP_SHARED;
	void* hint = (startAddr && *startAddr) ? *startAddr : NULL;

	if (addrSpec == B_EXACT_ADDRESS && hint)
		flags |= MAP_FIXED;

	void* address = mmap(hint, size, prot, flags, memfd, 0);
	if (address == MAP_FAILED) {
		close(memfd);
		return B_NO_MEMORY;
	}

	if (addrSpec == B_EXACT_ADDRESS && hint && address != hint) {
		munmap(address, size);
		close(memfd);
		return B_BAD_VALUE;
	}

	struct nexus_area_create create = {};
	create.fd = memfd;
	strncpy(create.name, name, B_OS_NAME_LENGTH - 1);
	create.size = size;
	create.lock = lock;
	create.protection = protection;

	if (nexus_io(nexus, NEXUS_AREA_CREATE, &create) < 0
			|| create.ret != B_OK) {
		munmap(address, size);
		close(memfd);
		return create.ret != B_OK ? create.ret : B_ERROR;
	}

	BKernelPrivate::LocalArea local;
	local.id = create.area;
	local.name = name;
	local.address = address;
	local.size = size;
	local.protection = protection;
	local.memfd = memfd;
	BKernelPrivate::AreaPool::Get().Add(local);

	if (startAddr)
		*startAddr = address;

	return create.area;
}


area_id
clone_area(const char* name, void** destAddr, uint32 addrSpec,
	uint32 protection, area_id source)
{
	if (name == NULL || name[0] == '\0' || source < 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_clone clone = {
		.source = source,
		.protection = protection
	};
	strncpy(clone.name, name, B_OS_NAME_LENGTH - 1);

	if (nexus_io(nexus, NEXUS_AREA_CLONE, &clone) < 0)
		return B_ERROR;
	if (clone.ret != B_OK)
		return clone.ret;

	int prot = BKernelPrivate::protection_to_prot(protection);
	int flags = MAP_SHARED;
	void* hint = (destAddr && *destAddr) ? *destAddr : NULL;

	if (addrSpec == B_EXACT_ADDRESS && hint)
		flags |= MAP_FIXED;
	else if (addrSpec == B_CLONE_ADDRESS) {
		BKernelPrivate::LocalArea src;
		if (BKernelPrivate::AreaPool::Get().Get(source, src))
			hint = src.address;
	}

	void* address = mmap(hint, clone.size, prot, flags, clone.fd, 0);
	if (address == MAP_FAILED) {
		close(clone.fd);
		struct nexus_area_delete del = { .area = clone.area };
		nexus_io(nexus, NEXUS_AREA_DELETE, &del);
		return B_NO_MEMORY;
	}

	if (addrSpec == B_EXACT_ADDRESS && hint && address != hint) {
		munmap(address, clone.size);
		close(clone.fd);
		struct nexus_area_delete del = { .area = clone.area };
		nexus_io(nexus, NEXUS_AREA_DELETE, &del);
		return B_BAD_VALUE;
	}

	BKernelPrivate::LocalArea local;
	local.id = clone.area;
	local.name = name;
	local.address = address;
	local.size = clone.size;
	local.protection = protection;
	local.memfd = clone.fd;
	BKernelPrivate::AreaPool::Get().Add(local);

	if (destAddr)
		*destAddr = address;

	return clone.area;
}


status_t
delete_area(area_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	BKernelPrivate::LocalArea local;
	if (BKernelPrivate::AreaPool::Get().Remove(id, local)) {
		if (local.address && local.address != MAP_FAILED)
			munmap(local.address, local.size);
		if (local.memfd >= 0)
			close(local.memfd);
	}

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_delete del = { .area = id, .ret = B_OK };
	if (nexus_io(nexus, NEXUS_AREA_DELETE, &del) < 0)
		return B_ERROR;
	return del.ret;
}


area_id
find_area(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	// TODO
	area_id local = BKernelPrivate::AreaPool::Get().FindByName(name);
	if (local >= 0)
		return local;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_find find = {};
	strncpy(find.name, name, B_OS_NAME_LENGTH - 1);

	if (nexus_io(nexus, NEXUS_AREA_FIND, &find) < 0)
		return B_ERROR;
	if (find.ret != B_OK)
		return find.ret;

	return find.area;
}


status_t
resize_area(area_id /*id*/, size_t /*newSize*/)
{
	return B_NOT_SUPPORTED;
}


status_t
set_area_protection(area_id id, uint32 protection)
{
	if (id < 0)
		return B_BAD_VALUE;

	BKernelPrivate::LocalArea local;
	if (!BKernelPrivate::AreaPool::Get().Get(id, local))
		return B_BAD_VALUE;

	int prot = BKernelPrivate::protection_to_prot(protection);
	if (mprotect(local.address, local.size, prot) < 0)
		return B_ERROR;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_set_protection sp = {
		.area = id,
		.protection = protection,
		.ret = B_OK
	};

	if (nexus_io(nexus, NEXUS_AREA_SET_PROTECTION, &sp) < 0)
		return B_ERROR;
	return sp.ret;
}


area_id
area_for(void* address)
{
	return BKernelPrivate::AreaPool::Get().FindByAddress(address);
}


status_t
_get_area_info(area_id id, area_info* info, size_t size)
{
	if (id < 0 || info == NULL || size != sizeof(area_info))
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_get_info gi = { .area = id };
	if (nexus_io(nexus, NEXUS_AREA_GET_INFO, &gi) < 0)
		return B_ERROR;
	if (gi.ret != B_OK)
		return gi.ret;

	info->area = id;
	strncpy(info->name, gi.name, B_OS_NAME_LENGTH);
	info->size = gi.size;
	info->lock = gi.lock;
	info->protection = gi.protection;
	info->team = gi.team;
	info->ram_size = gi.size;

	BKernelPrivate::LocalArea local;
	if (BKernelPrivate::AreaPool::Get().Get(id, local))
		info->address = local.address;
	else
		info->address = NULL;

	return B_OK;
}


// smaps needs PTRACE_MODE_READ_FSCREDS, so it only works for same-uid teams;
// statm has no ptrace check and works for any team, at the cost of per-area
// granularity. The caller must drain the enumeration or the state leaks.
status_t
_get_next_area_info(team_id team, ssize_t* cookie, area_info* areaInfo,
	size_t size)
{
	if (cookie == NULL || *cookie < 0 || areaInfo == NULL
		|| size != sizeof(area_info)) {
		return B_BAD_VALUE;
	}

	if (team == 0)
		team = getpid();

	using namespace BKernelPrivate;

	// Cap on the smaps slurp; over the cap the enumeration fails instead of
	// growing an unbounded allocation.
	static const size_t kMaxSmapsBytes = 64 * 1024 * 1024;

	AreaScanState* state;
	if (*cookie == 0) {
		struct proc_dir dir;
		if (!proc_dir_open_pid(&dir, (pid_t)team))
			return B_BAD_TEAM_ID;

		int fd = proc_dir_open_file(&dir, "smaps");
		if (fd < 0) {
			if (errno != EACCES && errno != EPERM) {
				proc_dir_close(&dir);
				return B_BAD_TEAM_ID;
			}

			// smaps is off-limits for this team; fall back to the
			// world-readable statm whole-team totals.
			int statmFd = proc_dir_open_file(&dir, "statm");
			proc_dir_close(&dir);
			if (statmFd < 0) {
				if (errno == ENOENT)
					return B_BAD_TEAM_ID;
				return B_PERMISSION_DENIED;
			}

			char statmBuf[256];
			ssize_t statmLen = proc_read_all(statmFd, statmBuf,
				sizeof(statmBuf));
			close(statmFd);
			if (statmLen <= 0)
				return B_PERMISSION_DENIED;

			// proc(5): size resident shared text lib data dt (pages).
			unsigned long long pages = 0, residentPages = 0;
			unsigned long long sharedPages = 0;
			if (sscanf(statmBuf, "%llu %llu %llu", &pages, &residentPages,
					&sharedPages) < 3) {
				return B_PERMISSION_DENIED;
			}

			unsigned long long privatePages =
				(residentPages > sharedPages)
					? residentPages - sharedPages : 0;

			long pageSize = proc_page_size();

			state = new (std::nothrow) AreaScanState();
			if (state == NULL)
				return B_NO_MEMORY;
			state->mode = kAreaScanStatm;
			state->team = team;
			state->buffer = NULL;
			state->length = 0;
			state->offset = 0;
			state->privateBytes = privatePages * (unsigned long long)pageSize;
			state->sharedBytes = sharedPages * (unsigned long long)pageSize;
			state->statmNext = 0;
		} else {
			char* buffer = NULL;
			size_t length = 0;
			if (!proc_slurp(fd, &buffer, &length, kMaxSmapsBytes)) {
				int error = errno;
				close(fd);
				proc_dir_close(&dir);
				return error == EOVERFLOW ? B_NO_MEMORY : B_ERROR;
			}
			close(fd);
			proc_dir_close(&dir);

			state = new (std::nothrow) AreaScanState();
			if (state == NULL) {
				free(buffer);
				return B_NO_MEMORY;
			}
			state->mode = kAreaScanSmaps;
			state->buffer = buffer;
			state->length = length;
			state->offset = 0;
			state->team = team;
		}

		*cookie = (ssize_t)proc_enum_alloc(state);
		if (*cookie == 0) {
			free(state->buffer);
			delete state;
			return B_NO_MEMORY;
		}
	} else {
		state = (AreaScanState*)proc_enum_state((int32)*cookie);
		if (state == NULL)
			return B_BAD_VALUE;
	}

	if (state->mode == kAreaScanStatm) {
		while (state->statmNext < 2) {
			int which = state->statmNext++;
			uint64 bytes = (which == 0) ? state->privateBytes
				: state->sharedBytes;
			if (bytes == 0)
				continue;

			memset(areaInfo, 0, sizeof(area_info));
			areaInfo->area = B_ERROR;
			strncpy(areaInfo->name, (which == 0) ? "[private]" : "[shared]",
				B_OS_NAME_LENGTH - 1);
			areaInfo->name[B_OS_NAME_LENGTH - 1] = '\0';
			areaInfo->size = (size_t)bytes;
			areaInfo->lock = B_NO_LOCK;
			areaInfo->protection = (which == 0)
				? (B_READ_AREA | B_WRITE_AREA) : B_READ_AREA;
			areaInfo->team = state->team;
			areaInfo->ram_size = (uint32)bytes;
			areaInfo->address = NULL;

			return B_OK;
		}

		delete (AreaScanState*)proc_enum_release((int32)*cookie);
		*cookie = 0;
		return B_BAD_VALUE;
	}

	char line[ProcLineReader::kMaxLine];
	unsigned long long start = 0, end = 0;
	char perms[8] = {0};
	char pathname[PATH_MAX] = {0};
	unsigned long rssKb = 0;
	bool haveRecord = false;

	while (state->offset < state->length) {
		size_t lineStart = state->offset;
		size_t nl = lineStart;
		while (nl < state->length && state->buffer[nl] != '\n')
			nl++;
		size_t nextOffset = (nl < state->length) ? nl + 1 : state->length;

		size_t lineLen = nl - lineStart;
		if (lineLen >= sizeof(line))
			lineLen = sizeof(line) - 1;
		memcpy(line, state->buffer + lineStart, lineLen);
		line[lineLen] = '\0';

		struct proc_map_line map;
		bool isHeader = proc_parse_map_header(line, &map)
			&& map.end > map.start;

		if (isHeader && haveRecord)
			break;

		state->offset = nextOffset;

		if (isHeader) {
			haveRecord = true;
			start = map.start;
			end = map.end;
			strlcpy(perms, map.perms, sizeof(perms));

			if (map.pathLength > 0) {
				size_t copyLength = map.pathLength;
				if (copyLength >= sizeof(pathname))
					copyLength = sizeof(pathname) - 1;
				memcpy(pathname, map.path, copyLength);
				pathname[copyLength] = '\0';
			} else
				pathname[0] = '\0';
		} else if (haveRecord && strncmp(line, "Rss:", 4) == 0) {
			sscanf(line + 4, "%lu", &rssKb);
		}
	}

	if (!haveRecord) {
		free(state->buffer);
		delete (AreaScanState*)proc_enum_release((int32)*cookie);
		*cookie = 0;
		return B_BAD_VALUE;
	}

	char name[B_OS_NAME_LENGTH];
	if (pathname[0] == '\0') {
		strncpy(name, "[anon]", sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	} else if (strncmp(pathname, "/memfd:", 7) == 0) {
		char* n = pathname + 7;
		char* deleted = strstr(n, " (deleted)");
		if (deleted != NULL)
			*deleted = '\0';
		strncpy(name, n, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	} else if (pathname[0] == '[') {
		// Pseudo mapping such as [heap], [stack], [vdso]: keep as-is.
		strncpy(name, pathname, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	} else {
		const char* base = strrchr(pathname, '/');
		base = (base != NULL) ? base + 1 : pathname;
		strncpy(name, base, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	}

	uint32 protection = 0;
	if (perms[0] == 'r')
		protection |= B_READ_AREA;
	if (perms[1] == 'w')
		protection |= B_WRITE_AREA;
	if (perms[2] == 'x')
		protection |= B_EXECUTE_AREA;
	if (perms[3] == 's')
		protection |= B_CLONEABLE_AREA;

	area_id areaId = B_ERROR;
	if (team == getpid()) {
		area_id local = BKernelPrivate::AreaPool::Get().FindByAddress(
			(void*)(uintptr_t)start);
		if (local != B_ERROR)
			areaId = local;
	}

	memset(areaInfo, 0, sizeof(area_info));
	areaInfo->area = areaId;
	snprintf(areaInfo->name, sizeof(areaInfo->name), "%s", name);
	areaInfo->size = (size_t)(end - start);
	areaInfo->lock = B_NO_LOCK;
	areaInfo->protection = protection;
	areaInfo->team = team;
	areaInfo->ram_size = (uint32)(rssKb * 1024);
	areaInfo->address = (void*)(uintptr_t)start;

	// The cookie keeps the enumeration handle assigned at open time.
	return B_OK;
}


status_t
_kern_reserve_address_range(unsigned long* address, uint32 addressSpec,
	unsigned long size)
{
	if (size == 0)
		return B_BAD_VALUE;

	void* hint = address ? (void*)*address : NULL;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

	if (addressSpec == B_EXACT_ADDRESS && hint)
		flags |= MAP_FIXED;

	void* addr = mmap(hint, size, PROT_NONE, flags, -1, 0);
	if (addr == MAP_FAILED)
		return B_NO_MEMORY;

	if (addressSpec == B_EXACT_ADDRESS && hint && addr != hint) {
		munmap(addr, size);
		return B_BAD_VALUE;
	}

	if (address)
		*address = (unsigned long)addr;

	return B_OK;
}


area_id
_kern_transfer_area(area_id id, void** _address, uint32 addressSpec,
	team_id target)
{
	if (id < 0)
		return B_BAD_VALUE;

	// You can't transfer an area to your team
	if (target == (team_id)getpid())
		return B_NOT_ALLOWED;

	BKernelPrivate::LocalArea local;
	if (!BKernelPrivate::AreaPool::Get().Get(id, local))
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_transfer tr = {
		.area   = id,
		.target = target,
	};

	if (nexus_io(nexus, NEXUS_AREA_TRANSFER, &tr) < 0)
		return B_ERROR;
	if (tr.ret != B_OK)
		return tr.ret;

	if (_address)
		*_address = local.address;

	return tr.new_area;
}


} // extern "C"
