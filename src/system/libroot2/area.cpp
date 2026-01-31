/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#define _GNU_SOURCE

#include <KernelExport.h>
#include <OS.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <map>
#include <string>

#include "MutexLock.h"
#include "../kernel/nexus/nexus/nexus.h"
#include "Team.h"


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

	status_t ret = nexus_io(nexus, NEXUS_AREA_CREATE, &create);
	if (ret != B_OK) {
		munmap(address, size);
		close(memfd);
		return ret;
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

	status_t ret = nexus_io(nexus, NEXUS_AREA_CLONE, &clone);
	if (ret != B_OK)
		return ret;

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

	struct nexus_area_delete del = { .area = id };
	return nexus_io(nexus, NEXUS_AREA_DELETE, &del);
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

	status_t ret = nexus_io(nexus, NEXUS_AREA_FIND, &find);
	if (ret != B_OK)
		return B_NAME_NOT_FOUND;

	return find.area;
}


status_t
resize_area(area_id id, size_t newSize)
{
	if (id < 0 || newSize == 0)
		return B_BAD_VALUE;

	BKernelPrivate::LocalArea local;
	if (!BKernelPrivate::AreaPool::Get().Get(id, local))
		return B_BAD_VALUE;

	size_t pageSize = sysconf(_SC_PAGESIZE);
	newSize = (newSize + pageSize - 1) & ~(pageSize - 1);

	if (ftruncate(local.memfd, newSize) < 0)
		return B_ERROR;

	void* newAddr = mremap(local.address, local.size, newSize, MREMAP_MAYMOVE);
	if (newAddr == MAP_FAILED)
		return B_NO_MEMORY;

	BKernelPrivate::AreaPool::Get().Update(id, newAddr, newSize);

	int nexus = BKernelPrivate::Team::GetAreaDescriptor();
	if (nexus < 0)
		return B_ERROR;

	struct nexus_area_resize resize = { .area = id, .new_size = newSize };
	return nexus_io(nexus, NEXUS_AREA_RESIZE, &resize);
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
		.protection = protection
	};

	return nexus_io(nexus, NEXUS_AREA_SET_PROTECTION, &sp);
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
	status_t ret = nexus_io(nexus, NEXUS_AREA_GET_INFO, &gi);
	if (ret != B_OK)
		return B_BAD_VALUE;

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


status_t
_get_next_area_info(team_id team, ssize_t* cookie, area_info* areaInfo,
	size_t size)
{
	// TODO
	return B_BAD_VALUE;
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
		munmap(address, size);
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
	// TODO
	return B_ERROR;
}


} // extern "C"
