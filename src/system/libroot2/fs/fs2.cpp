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


namespace BKernelPrivate {


static pthread_mutex_t sLock;
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

	// TODO: fd = AT_FDCWD;

	if (fd >= 0) {
		char buf[PATH_MAX];
		size_t size = PATH_MAX;
		std::string proc = "/proc/self/fd/";
		proc.append(std::to_string(fd));
		ssize_t bytes = readlink(proc.c_str(), buf, size);
		if (bytes < 0)
			return B_ERROR;

		path = std::string(buf, bytes);

		if (name != NULL) {
			path += '/';
			path += name;
		}

	} else if (name != NULL) {
		char buf[B_PATH_NAME_LENGTH];
		_kern_normalize_path(name, true, buf);
		path = std::string(buf);
	}

	TRACE("getPath %s fd %d\n", path.c_str(), fd);
	return B_OK;
}


status_t
getPath(ino_t node, const char* name, std::string& path)
{
	if (node < 0 && name == NULL)
		return B_BAD_VALUE;

	if (node > 0 && name != NULL && name[0] == '/' && strlen(name) == 1)
		name = NULL;

	TRACE("getPath %ld %s\n", node, name);

	pthread_mutex_lock(&sLock);
	auto elem = gMap.find(node);
	if (elem != end(gMap)) {
		TRACE("getPath %d %s %s\n", node, elem->second.c_str(), name);
		std::string ret(elem->second.c_str());
		if (name != NULL) {
			ret += '/';
			ret += name;
		}
		path = ret;
		pthread_mutex_unlock(&sLock);
		return B_OK;
	}
	pthread_mutex_unlock(&sLock);

	return B_ERROR;
}


static void
insertPath(ino_t inode, std::string path) {
	pthread_mutex_lock(&sLock);
	gMap.insert(std::make_pair(inode, path));
	pthread_mutex_unlock(&sLock);
}


}


status_t
_kern_read_stat(int fd, const char* path, bool traverseLink,
	struct stat* st, size_t statSize)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	std::string destPath;
	status_t ret = BKernelPrivate::getPath(fd, path, destPath);
	if (ret != B_OK)
		return B_ENTRY_NOT_FOUND;

	int result;
	if (traverseLink)
		result = stat(destPath.c_str(), st);
	else
		result = lstat(destPath.c_str(), st);

	if (result < 0)
		return B_ENTRY_NOT_FOUND;
	else
		BKernelPrivate::insertPath(st->st_ino, destPath);

	return B_OK;
}


int
_kern_open(int fd, const char* path, int openMode, int perms)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ERROR;

	// TODO see BNode::_SetTo

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	std::string destPath;
	status_t ret = BKernelPrivate::getPath(fd, path, destPath);

	if (strlen(path) >= B_FILE_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	struct stat st;
	if (lstat(destPath.c_str(), &st) < 0) {
		if (!(openMode & O_CREAT))
			return B_ENTRY_NOT_FOUND;
	}

	return openat(fd, path, openMode, perms);
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

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	int ret = 0;
	if (fd == -1 && path[0] != NULL) {
		DIR* dir = opendir(path);
		if (dir == NULL)
			return B_ENTRY_NOT_FOUND;

		ret = dirfd(dir);
	} else
		ret = openat(fd, path, O_RDWR);

	if (ret < 0)
		return B_ENTRY_NOT_FOUND;

	return ret;
}


int
_kern_open_parent_dir(int fd, char* name, size_t length)
{
	CALLED();

	if (fd < 0 && name == NULL)
		return B_ERROR;

	std::string path;
	if (BKernelPrivate::getPath(fd, NULL, path) == B_OK) {
		char* destPath = NULL;
		if (path == "/") {
			// TODO
			//strcpy(name, ".");
			return _kern_open_dir(-1, "/");
		} else {
			char* dirPath = dirname(strdup(path.c_str()));
			if (dirPath == NULL)
				return B_ERROR;

			TRACE("the path %s\n", dirPath);

			DIR* dir = opendir(dirPath);
			if (dir == NULL)
				return B_ERROR;

			char* destPath = basename(dirPath);

			if (destPath == NULL) {
				closedir(dir);
				return B_ERROR;
			}

			if (strlcpy(name, destPath, strlen(destPath))
					>= length) {
				closedir(dir);
				return B_BUFFER_OVERFLOW;
			}
			int ret = dirfd(dir);
			free(dir);
			return ret;
		}
	}

	return B_ERROR;
}


int
_kern_open_dir_entry_ref(dev_t device, ino_t node, const char* name)
{
	CALLED();

	std::string path;
	if (BKernelPrivate::getPath(node, name, path) == B_OK) {
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
	if (BKernelPrivate::getPath(node, name, path) == B_OK) {
		int ret = open(path.c_str(), openMode, perms);
		if (ret < 0)
			return B_ENTRY_NOT_FOUND;

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
		//return B_ENTRY_NOT_FOUND;

	std::string destPath;
	status_t ret = BKernelPrivate::getPath(node, leaf, destPath);
	if (ret == B_OK) {
		if (strlcpy(userPath, destPath.c_str(),
				pathLength) >= pathLength) {
			return B_BUFFER_OVERFLOW;
		}
 	}
	return ret;
}


status_t
_kern_normalize_path(const char* userPath, bool _ignored, char* buffer)
{
	CALLED();

	status_t ret = B_OK;
	char* path = realpath(userPath, NULL);
	if (path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (strlcpy(buffer, path, B_PATH_NAME_LENGTH)
			>= B_PATH_NAME_LENGTH) {
		ret = B_BUFFER_OVERFLOW;
	}
	free(path);
	return ret;
}


status_t
_kern_read_link(int fd, const char* path, char* buffer, size_t* _bufferSize)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	ssize_t size = readlinkat(fd, path, buffer, *_bufferSize);
	if (size < 0)
		return B_ERROR;

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

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	return B_OK;
}


ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize,
	uint32 maxCount)
{
	CALLED();

	if (fd < 0 || buffer == NULL || bufferSize <= 0 || maxCount <= 0)
		return B_BAD_VALUE;

	struct linux_dirent {
		long           d_ino;
		off_t          d_off;
		unsigned short d_reclen;
		char           d_name[];
	};

	struct linux_dirent buf[maxCount+1];
	size_t bufSize = sizeof(struct linux_dirent)*(maxCount+1);

	ssize_t ret = syscall(SYS_getdents, fd, &buf, bufSize);
	if (ret == 0)
		return B_OK;

	if (ret < 0)
		return B_ERROR;

	ssize_t retCount = ret/sizeof(struct linux_dirent);
	for (uint32 i = 0; i < maxCount && i < retCount; i++) {
		buffer[i].d_ino = buf[i].d_ino;
		buffer[i].d_off = buf[i].d_off;
		buffer[i].d_reclen = buf[i].d_reclen;
		strcpy(buffer[i].d_name, buf[i].d_name);
	}

	return retCount;
}


off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	CALLED();

	off_t ret = lseek(fd, pos, seekType);
	if (ret < 0)
		return errno;

	return ret;
}


ssize_t
_kern_read(int fd, off_t pos, void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t size = pread(fd, buffer, bufferSize, pos);
	return (size < 0) ? errno : size;
}


ssize_t
_kern_write(int fd, off_t pos, const void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t size = pwrite(fd, buffer, bufferSize, pos);
	return (size < 0) ? errno : size;
}


status_t
_kern_rewind_dir(int fd)
{
	CALLED();

	if (fd == -1)
		return B_BAD_VALUE;

	DIR* dir = fdopendir(dup(fd));
	if (dir == NULL)
		return B_BAD_VALUE;

	rewinddir(dir);
	closedir(dir);
	return B_OK;
}


status_t
_kern_remove_dir(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	int ret = unlinkat(fd, path, AT_REMOVEDIR);

	// TODO: remap errors
	if (ret < 0 && errno == ENOTEMPTY)
		return B_DIRECTORY_NOT_EMPTY;

	return (ret < 0) ? B_ENTRY_NOT_FOUND : B_OK;
}


status_t
_kern_rename(int oldDir, const char* oldPath, int newDir, const char* newPath)
{
	CALLED();

	return (renameat(oldDir, oldPath, newDir, newPath) < 0) ? B_OK : B_BAD_VALUE;
}


int
_kern_dup(int fd)
{
	CALLED();

	return (fd < 0) ? -1 : dup(fd);
}


status_t
_kern_create_dir(int fd, const char* path, int perms)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	TRACE("create dir %d %s\n", fd, path);

	if (mkdirat(fd, path, perms) < 0)
		return errno;

	return B_OK;
}


status_t
_kern_create_symlink(int fd, const char* path, const char* toPath, int mode)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	std::string temp;
	status_t ret = BKernelPrivate::getPath(fd, path, temp);
	if (ret != B_OK)
		return ret;

	return (symlink(temp.c_str(), toPath) < 0) ? errno : B_OK;
}


status_t
_kern_unlink(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ENTRY_NOT_FOUND;

	if (fd == -1 && path[0] != '/')
		fd = AT_FDCWD;

	return (unlinkat(fd, path, 0) < 0) ? B_ENTRY_NOT_FOUND : B_OK;
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
	CALLED();

	return _kern_read(fd, pos, buffer, count);
}


ssize_t
write_pos(int fd, off_t pos, const void* buffer,size_t count)
{
	CALLED();

	return _kern_write(fd, pos, buffer, count);
}
