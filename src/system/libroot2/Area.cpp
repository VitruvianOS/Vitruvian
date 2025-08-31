/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <KernelExport.h>
#include <map>
#include <string>

#include <dirent.h>
#include <sys/file.h>
#include <sys/mman.h>

#include "KernelDebug.h"
#include "MutexLock.h"


namespace BKernelPrivate {


#define AREA_PATH "/dev/shm/vproc/area/"
#define VPROC_PATH "/dev/shm/vproc/"

class Area {
public:
					Area()
					{
					};

					~Area()
					{
					};

	area_id			Create(const char* name, void** startAddr, uint32 addrSpec,
						size_t size, uint32 lock, uint32 protection);

	area_id			Clone(const char* name, void** startAddr, uint32 addrSpec,
						uint32 protection, area_id source);

	void			Unmap();

	status_t		Resize(area_id id, size_t newSize);
	status_t		SetProtection(area_id id, uint32 protection);

	static status_t	ReserveAddressRange(addr_t* address, uint32 addressSpec,
						addr_t size);

private:
		int 		_PublishArea(const char* name, int oflag);
		bool		_CheckLock(const char* path);

		area_id		fId;
		std::string fName;
		std::string fPath;
		const char* fLockPath;
		int			fLock;

		void*		fAddress;
		size_t		fSize;
};


class AreaPool {
public:
	static area_id Create(const char* name, void** startAddr, uint32 addrSpec,
		size_t size, uint32 lock, uint32 protection)
	{
		BKernelPrivate::MutexLocker locker(&fLock);
	
		Area* area = new Area();
		area_id id = area->Create(name, startAddr, addrSpec,
			size, lock, protection);
		if (id < 0) {
			delete area;
			return id;
		}
		fAreas.insert(std::make_pair(id, area));
		return id;
	}

	static area_id Clone(const char* name, void** destAddr, uint32 addrSpec,
		uint32 protection, area_id source)
	{
		BKernelPrivate::MutexLocker locker(&fLock);

		Area* area = new Area();
		area_id id = area->Clone(name, destAddr, addrSpec,
			protection, source);

		if (id < 0) {
			delete area;
			return id;
		}
		fAreas.insert(std::make_pair(id, area));
		return id;
	}

	static status_t Delete(area_id id)
	{
		BKernelPrivate::MutexLocker locker(&fLock);

		Area* area = FindLocalArea(id);
		if (area == NULL)
			return B_BAD_VALUE;

		area->Unmap();
		delete area;
						
		return B_OK;		
	}

	static status_t Resize(area_id id, size_t newSize)
	{
		BKernelPrivate::MutexLocker locker(&fLock);

		Area* area = FindLocalArea(id);
		if (area == NULL)
			return B_BAD_VALUE;

		status_t ret = area->Resize(id, newSize);
		if (ret != B_OK) {
			delete area;
			return ret;
		}
		return B_OK;
	}
	
	static status_t	SetProtection(area_id id, uint32 protection)
	{
		BKernelPrivate::MutexLocker locker(&fLock);
	
		Area* area = FindLocalArea(id);
		if (area == NULL)
			return B_BAD_VALUE;
	
		status_t ret = area->SetProtection(id, protection);
		if (ret != B_OK)
			return ret;
	
		if (id < 0)
			delete area;
	
		return B_OK;
	}
	
	static Area* FindLocalArea(area_id id)
	{
		if (id < 0)
			return NULL;

		auto ret = fAreas.find(id);
		if (ret == end(fAreas))
			return NULL;
		return ret->second;
	}

	static status_t	FindPath(area_id id, std::string& path)
	{
		if (id < 0)
			return B_BAD_VALUE;

		DIR* dir = opendir(AREA_PATH);
		if (dir == NULL) {
			TRACE("Error opening vproc %s", strerror(errno));
			debugger("Unable to open area directory");
			return B_ERROR;
		}

		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_ino == id) {
				closedir(dir);
				path = AREA_PATH;
				path.append(entry->d_name);

				TRACE("Found area name %s\n", entry->d_name);
				return B_OK;
			}
		}

		closedir(dir);
		return B_ERROR;
	}

#if 0
	static area_id FindName(const char* name)
	{
		if (name == NULL)
			return B_BAD_VALUE;

		DIR* dir = opendir(AREA_PATH);
		if (dir == NULL) {
			TRACE("Error opening vproc %s", strerror(errno));
			debugger("Unable to open area directory");
			return B_ERROR;
		}

		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, name) == 0) {
				closedir(dir);
				return entry->d_ino;
			}
		}

		closedir(dir);
		return B_ERROR;
	}
#endif

	//static status_t	GetNextAreaInfo();
	//static status_t	GetAreaInfo();

	static area_id	AreaFor(void* address)
	{
		// TODO: Implement me
		return B_ERROR;
	}

private:
	static std::map<area_id, Area*>	fAreas;
	static pthread_mutex_t			fLock;
};

std::map<area_id, Area*> AreaPool::fAreas;
pthread_mutex_t AreaPool::fLock = PTHREAD_MUTEX_INITIALIZER;


area_id
Area::Create(const char* name, void** startAddr, uint32 addrSpec,
	size_t size, uint32 lock, uint32 protection)
{
	if (size <= 0 || name == NULL)
		return B_BAD_VALUE;

	// TODO: implement memory locking

	if (size <= 0)
		return B_BAD_VALUE;

	int handle = _PublishArea(name, O_RDWR);
	if (handle == -1)
		return B_NO_MEMORY;

	if (ftruncate(handle, size) != 0) {
		close(handle);
		unlink(fPath.c_str());
		return B_NO_MEMORY;
	}

	int prot = PROT_READ;
	if (protection & B_WRITE_AREA || protection & B_KERNEL_WRITE_AREA)
		prot |= PROT_WRITE;

	void* address = NULL;
	if (startAddr != NULL)
		address = *startAddr;

	if ((fAddress = mmap(address, size, prot, MAP_SHARED, handle, 0))
			== MAP_FAILED) {
		close(handle);
		unlink(fPath.c_str());
		return (errno == ENOMEM ? B_NO_MEMORY : B_ERROR);
	}
	close(handle);

	TRACE("mmapped area %s at %p size %d\n", fName.c_str(), fAddress, size);

	struct stat st;
	if (stat(fPath.c_str(), &st) == -1 || st.st_size <= 0)
		return B_NO_MEMORY;

	fId = st.st_ino;
	fSize = size;

	if (startAddr != NULL)
		*startAddr = fAddress;

	return fId;
}


area_id
Area::Clone(const char* name, void** startAddr, uint32 addrSpec,
	uint32 protection, area_id source)
{
	if (name == NULL || source < 0)
		return B_BAD_VALUE;

	std::string sourcePath;
	status_t err = AreaPool::FindPath(source, sourcePath);
	if (err != B_OK)
		return err;

	std::string lockPath = sourcePath;
	lockPath.append("_lock");
	if (!_CheckLock(lockPath.c_str()))
		return B_BAD_VALUE;

	int oflag = O_RDONLY;
	int prot = PROT_READ;
	if (protection & B_WRITE_AREA || protection & B_KERNEL_WRITE_AREA) {
		prot |= PROT_WRITE;
		oflag = O_RDWR;
	}

	int handler = open(sourcePath.c_str(), oflag, 0);
	if (handler < 0)
		return B_BAD_VALUE;

	struct stat st;
	if (fstat(handler, &st) == -1 || st.st_size <= 0) {
		close(handler);
		return B_NO_MEMORY;
	}

	// TODO: addrSpec

	void* address = NULL;
	if (startAddr != NULL)
		address = *startAddr;

	fAddress = mmap(address, st.st_size, prot, MAP_SHARED, handler, 0);
	if (fAddress == MAP_FAILED) {
		close(handler);
		return (errno == ENOMEM ? B_NO_MEMORY : B_ERROR);
	}

	int link = _PublishArea(name, oflag);
	if (link == -1)
		return B_NO_MEMORY;

	// TODO: we shouldn't unlink here
	if (unlink(fPath.c_str()) == -1)
		return B_ERROR;

	if (linkat(handler, "", AT_FDCWD, fPath.c_str(), AT_EMPTY_PATH) == -1) {
		printf("err %s\n", strerror(errno));
		return B_ERROR;
	}
	close(handler);

	if (stat(fPath.c_str(), &st) == -1 || st.st_size <= 0)
		return B_NO_MEMORY;

	fId = st.st_ino;
	fSize = st.st_size;

	if (startAddr != NULL)
		*startAddr = fAddress;

	TRACE("cloned area %s at %p size %d\n", fName, fAddress, st.st_size);

	return fId;
}


void
Area::Unmap()
{
	munmap(fAddress, fSize);
	unlink(fPath.c_str());

	// TODO: unlink lock

	flock(fLock, LOCK_UN);
	close(fLock);
}


status_t
Area::Resize(area_id id, size_t newSize)
{
	if (id < 0 || newSize < 0)
		return B_BAD_VALUE;

	int handle = open(fPath.c_str(), O_RDWR, 0);
	if (handle == -1)
		return B_ERROR;

	if (ftruncate(handle, newSize) == 0) {
		// TODO: implement memory locking flags
		void* newAddr = mremap(fAddress, fSize, newSize, MREMAP_MAYMOVE);
		if (newAddr == MAP_FAILED) {
			close(handle);
			return (errno == ENOMEM ? B_NO_MEMORY : B_ERROR);
		}
		close(handle);

		fSize = newSize;
		fAddress = newAddr;
		return B_OK;
	}

	close(handle);
	return B_ERROR;
}


status_t
Area::SetProtection(area_id id, uint32 protection)
{
	if (id < 0)
		return B_BAD_VALUE;

	uint32 posixProtection = PROT_READ;

	if (protection & B_WRITE_AREA)
		posixProtection |= PROT_WRITE;

	if (mprotect(fAddress, fSize, posixProtection) != 0)
		return B_ERROR;

	return B_OK;
}


status_t
Area::ReserveAddressRange(addr_t* address, uint32 addressSpec,
	addr_t size)
{
	if (size == 0)
		return B_BAD_VALUE;

	void* addr = mmap((void*) address, size,
		PROT_NONE, MAP_NORESERVE, 0, 0);

	if (addr == MAP_FAILED)
		return (errno == ENOMEM ? B_NO_MEMORY : B_ERROR);

	return B_OK;
}


int
Area::_PublishArea(const char* name, int oflag)
{
	fName = name;

    struct stat st;

	if (stat(AREA_PATH, &st) != 0) {
		mkdir(VPROC_PATH, 0700);
		mkdir(AREA_PATH, 0700);
	}

	std::string path = AREA_PATH;
	path.append(name);
	path.append(".XXXXXX");

	char tmp[B_FILE_NAME_LENGTH];
	strncpy(tmp, path.c_str(), B_FILE_NAME_LENGTH);

	int ret = mkstemp(tmp);
	if (ret < 0)
		return -1;

	fPath = std::string(tmp);
	path = std::string(fPath);
	path.append("_lock");
	fLock = creat(path.c_str(), oflag);
	if (fLock < 0) {
		TRACE("path %s %s %s\n", path.c_str(), strerror(errno), fPath.c_str());
		return -1;
	}

	if (flock(fLock, LOCK_EX | LOCK_NB) == -1)
		return -1;

	fLockPath = strdup(path.c_str());

	return ret;
}


bool
Area::_CheckLock(const char* path)
{
	int lock = open(path, O_RDWR, 0);
	if (lock < 0)
		return false;

	if (flock(lock, LOCK_EX | LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK) {
			close(lock);
			return true;
		}
	} else
		flock(lock, LOCK_UN);

	close(lock);
	return false;
}


}


extern "C" {


area_id
create_area(const char* name, void** startAddr, uint32 addrSpec,
	size_t size, uint32 lock, uint32 protection)
{
	CALLED();

	return BKernelPrivate::AreaPool::Create(name, startAddr, addrSpec,
		size, lock, protection);
}


area_id
clone_area(const char* name, void** destAddr, uint32 addrSpec,
	uint32 protection, area_id source)
{
	CALLED();

	return BKernelPrivate::AreaPool::Clone(name, destAddr, addrSpec,
		protection, source);
}


status_t
delete_area(area_id id)
{
	CALLED();

	return BKernelPrivate::AreaPool::Delete(id);
}


#if 0
area_id
find_area(const char* name)
{
	CALLED();

	return BKernelPrivate::AreaPool::FindName(name);
}
#endif

status_t
resize_area(area_id id, size_t newSize)
{
	CALLED();

	return BKernelPrivate::AreaPool::Resize(id, newSize);
}


status_t
_get_area_info(area_id id, area_info* info, size_t size)
{
	CALLED();

	return B_ERROR;
}


status_t
_get_next_area_info(team_id team, ssize_t* cookie,
	area_info* areaInfo, size_t size)
{
	CALLED();

	return B_ERROR;
}


status_t
set_area_protection(area_id id, uint32 protection)
{
	CALLED();

	return BKernelPrivate::AreaPool::SetProtection(id, protection);
}


area_id
_kern_transfer_area(area_id id, void **_address, uint32 addressSpec,
	team_id target)
{
	UNIMPLEMENTED();
	return id;
}


status_t
_kern_reserve_address_range(addr_t* address, uint32 addressSpec,
	addr_t size)
{
	CALLED();
	return BKernelPrivate::Area::ReserveAddressRange(address,
		addressSpec, size);
}


area_id
area_for(void* address)
{
	CALLED();
	return BKernelPrivate::AreaPool::AreaFor(address);
}


}
