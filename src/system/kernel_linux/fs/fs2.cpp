/*
 *  Copyright 2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <Locker.h>

#include <dirent.h>
#include <libgen.h>
#include <sys/syscall.h>

#include <map>
#include <string>

#include "KernelDebug.h"

//TODO: add more args checking


namespace BPrivate {


static BLocker fLock;
static std::map<ino_t, std::string> gMap;


status_t
getPath(int fd, const char* name, std::string& path)
{
	if (fd < 0 && name == NULL) {
		return B_BAD_VALUE;
	} else if (fd < 0 && name != NULL && name[0] == '/') {
		path = std::string(name);
		return B_OK;
	} 

	if (fd >= 0) {
		char buf[PATH_MAX];
		size_t size = PATH_MAX;
		std::string proc = "/proc/self/fd/";
		proc.append(std::to_string(fd));
		ssize_t bytes = readlink(proc.c_str(), buf, size);
		if (bytes < 0)
			return B_ERROR;

		path = std::string(buf, bytes);
	}

	if (name != NULL) {
		path += '/';
		path += name;
	}

	TRACE("getPath %s fd %d\n", path.c_str(), fd);
	return B_OK;
}


status_t
getPath(ino_t node, const char* name, std::string& path)
{
	fLock.Lock();
	auto elem = gMap.find(node);
	if (elem != end(gMap)) {
		TRACE("getPath %d %s %s\n", node, elem->second.c_str(), name);
		std::string ret(elem->second.c_str());
		if (name != NULL) {
			ret += '/';
			ret += name;
		}
		path = ret;
		fLock.Unlock();
		return B_OK;
	}
	fLock.Unlock();
	return B_ERROR;
}


status_t
get_path(int fd, const char* name, std::string& path)
{
	return getPath(fd, name, path);
}


static void
insertPath(ino_t inode, std::string path) {
	fLock.Lock();
	gMap.insert(std::make_pair(inode, path));
	fLock.Unlock();
}


inline ssize_t
checkPosition(off_t pos, int fd, int seekType) {
	if (pos > 0) {
		off_t ret = lseek(fd, pos, seekType);
		if (ret < 0)
			return errno;
	}
	return B_OK;
}


}


status_t
_kern_read_stat(int fd, const char* path, bool traverseLink,
	struct stat* st, size_t statSize)
{
	CALLED();

	std::string destPath;
	status_t ret = BPrivate::getPath(fd, path, destPath);
	if (ret != B_OK)
		return ret;

	int result;
	if (traverseLink)
		result = stat(destPath.c_str(), st);
	else
		result = lstat(destPath.c_str(), st);

	BPrivate::insertPath(st->st_ino, destPath);

	if (result < 0)
		return result;

	return B_OK;
}


int
_kern_open(int fd, const char* path, int openMode, int perms)
{
	CALLED();

	std::string src;
	status_t err = BPrivate::getPath(fd, path, src);
	if (err != B_OK)
		return err;

	struct stat st;
	if (lstat(src.c_str(), &st) < 0 && !(openMode & O_CREAT))
		return errno;

	BPrivate::insertPath(st.st_ino, src.c_str());

	return open(src.c_str(), openMode, perms);
}


status_t
_kern_close(int fd)
{
	CALLED();

	return (close(fd) < 0) ? errno : B_OK;
}


int
_kern_open_dir(int fd, const char* path)
{
	CALLED();

	std::string temp;
	status_t ret = BPrivate::getPath(fd, path, temp);
	if (ret != B_OK)
		return ret;

	DIR* dir = opendir(temp.c_str());
	if (dir != NULL) {
		int result = dirfd(dir);
		free(dir);
		return result;
	}

	return errno;
}


int
_kern_open_parent_dir(int fd, char* name, size_t nameLength)
{
	CALLED();

	std::string path;
	if (BPrivate::getPath(fd, NULL, path) == B_OK) {
		char* dirPath = dirname(strdup(path.c_str()));
		if (dirPath == NULL)
			return B_ERROR;

		DIR* dir = opendir(dirPath);
		if (dir == NULL)
			return errno;

		char* destPath = basename(dirPath);
		if (destPath == NULL) {
			closedir(dir);
			return B_ERROR;
		}

		if (strlcpy(name, destPath, strlen(destPath))
				>= strlen(destPath)) {
			closedir(dir);
			return B_BUFFER_OVERFLOW;
		}

		int ret = dirfd(dir);
		free(dir);
		return ret;
	}

	return B_ERROR;
}


int
_kern_open_dir_entry_ref(dev_t device, ino_t node, const char* name)
{
	CALLED();

	std::string path;
	if (BPrivate::getPath(node, name, path) == B_OK) {
		DIR* dir = opendir(path.c_str());
		if (dir != NULL) {
			int result = dirfd(dir);
			free(dir);
			return result;
		}
	}

	return B_ERROR;
}


int
_kern_open_entry_ref(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	CALLED();

	std::string path;
	if (BPrivate::getPath(node, name, path) == B_OK) {
		int ret = open(path.c_str(), openMode, perms);
		if (ret < 0)
			return errno;

		return ret;
	}

	return B_ERROR;
}


status_t
_kern_entry_ref_to_path(dev_t device, ino_t node, const char* leaf,
	char* userPath, size_t pathLength)
{
	CALLED();

	if (pathLength <= 0)
		return B_ERROR;

	std::string destPath;
	status_t ret = BPrivate::getPath(node, leaf, destPath);
	if (ret == B_OK) {
		if (strlcpy(userPath, destPath.c_str(), pathLength) >= pathLength)
			return B_BUFFER_OVERFLOW;
 	}
	return ret;
}


status_t
_kern_normalize_path(const char* userPath, bool traverseLink, char* buffer)
{
	CALLED();
	// TODO: this is actually incomplete
	realpath(userPath, buffer);
	return B_OK;
}


status_t
_kern_read_link(int fd, const char* path, char* buffer, size_t* _bufferSize)
{
	CALLED();

	std::string destPath;
	status_t ret = BPrivate::getPath(fd, path, destPath);
	if (ret != B_OK)
		return ret;

	ssize_t size = readlink(destPath.c_str(), buffer, *_bufferSize);
	if (size < 0)
		return errno;

	*_bufferSize = size;
	return B_OK;
}


status_t
_kern_fsync(int fd)
{
	CALLED();

	return (fsync(fd) < 0) ? errno : B_OK ;
}


status_t
_kern_write_stat(int fd, const char* path, bool traverseLink,
	const struct stat* st, size_t statSize, int statMask)
{
	UNIMPLEMENTED();

	return B_OK;
}


ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize,
	uint32 maxCount)
{
	CALLED();

	if (maxCount <= 0 || buffer == NULL || bufferSize <= 0)
		return B_BAD_VALUE;
 
	return syscall(SYS_getdents, fd, buffer, bufferSize);
}


off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	CALLED();

	return BPrivate::checkPosition(fd, pos, seekType);
}


ssize_t
_kern_read(int fd, off_t pos, void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t ret = BPrivate::checkPosition(fd, pos, SEEK_SET);
	if (ret != B_OK)
		return ret;

	ssize_t size = read(fd, buffer, bufferSize);
	return (size < 0) ? errno : size;
}


ssize_t
_kern_write(int fd, off_t pos, const void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t ret = BPrivate::checkPosition(fd, pos, SEEK_SET);
	if (ret != B_OK)
		return ret;

	ssize_t size = write(fd, buffer, bufferSize);
	return (size < 0) ? errno : size;
}


status_t
_kern_rewind_dir(int fd)
{
	CALLED();

	if (fd < 0)
		return B_BAD_VALUE;

	DIR* dir = fdopendir(dup(fd));
	if (dir == NULL)
		return B_BAD_VALUE;

	rewinddir(dir);
	closedir(dir);
	return B_OK;
}



status_t
_kern_rename(int oldDir, const char* oldPath, int newDir, const char* newPath)
{
	CALLED();

	std::string dirPath;
	std::string newDirPath;

	status_t ret = BPrivate::getPath(oldDir, oldPath, dirPath);
	if (ret != B_OK)
		return ret;

	ret = BPrivate::getPath(newDir, newPath, newDirPath);
	if (ret != B_OK)
		return ret;

	return (rename(dirPath.c_str(), newDirPath.c_str()) < 0) ? B_OK : errno;
}


int
_kern_dup(int fd)
{
	CALLED();

	return (fd < 0) ? B_FILE_ERROR : dup(fd);
}


status_t
_kern_create_dir(int fd, const char* path, int perms)
{
	CALLED();

	std::string dirPath;
	status_t ret = BPrivate::getPath(fd, path, dirPath);
	if (ret == B_OK && mkdir(dirPath.c_str(), perms) < 0)
		return errno;

	return ret;
}


status_t
_kern_create_symlink(int fd, const char* path, const char* toPath, int mode)
{
	CALLED();

	std::string temp;
	status_t ret = BPrivate::getPath(fd, path, temp);
	if (ret != B_OK)
		return ret;

	return (symlink(toPath, temp.c_str()) < 0) ? errno : B_OK;
}


status_t
_kern_unlink(int fd, const char* path)
{
	CALLED();

	std::string destPath;
	status_t ret = BPrivate::getPath(fd, path, destPath);
	if (ret != B_OK)
		return ret;

	return (unlink(destPath.c_str()) < 0) ? errno : B_OK;
}


status_t
_kern_lock_node(int fd)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_unlock_node(int fd)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


ssize_t
read_pos(int fd, off_t pos, void* buffer, size_t count)
{
	return _kern_read(fd, pos, buffer, count);
}


ssize_t
write_pos(int fd, off_t pos, const void* buffer,size_t count)
{
	return _kern_write(fd, pos, buffer, count);
}
