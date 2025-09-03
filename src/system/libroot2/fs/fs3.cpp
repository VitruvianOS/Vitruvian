/*
 *  Copyright 2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <Locker.h>

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <syslimits.h>

#include <map>
#include <string>

#include "KernelDebug.h"

#define OPEN_BY_INODE_WORKAROUND 1

// TODO convert the convertible errno errors in return


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

	printf("getPath found %s fd %d\n", path.c_str(), fd);
	return B_OK;
}


status_t
getPath(ino_t node, const char* name, std::string& path)
{
	if (node < 0 && name == NULL)
		return B_BAD_VALUE;

	if (node > 0 && name != NULL && name[0] == '/' && strlen(name) == 1)
		name = NULL;

	printf("getPath for node %ld %s\n", node, name);

	pthread_mutex_lock(&sLock);
	auto elem = gMap.find(node);
	if (elem != end(gMap)) {
		printf("getPath for node %d %s %s\n", node, elem->second.c_str(), name);
		std::string ret(elem->second.c_str());
		if (name != NULL && strcmp(name, ".") != 0) {
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


// TODO: missing dev_t
static void
insertPath(ino_t inode, int fd, const char* path) {
	std::string destPath;
	status_t ret = BKernelPrivate::getPath(fd, path, destPath);
	if (ret != B_OK)
		return;

	pthread_mutex_lock(&sLock);
	gMap.insert(std::make_pair(inode, destPath));
	pthread_mutex_unlock(&sLock);
}


}


status_t
_kern_read_stat(int fd, const char* path, bool traverseLink,
	struct stat* st, size_t _ignored)
{
	CALLED();

	printf("read stat %d %s %d\n", fd, path, traverseLink);

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

	return fstatat(fd, path, st,
		traverseLink == false ? AT_SYMLINK_NOFOLLOW : 0) < 0 ?
			B_ENTRY_NOT_FOUND : B_OK;
}


int
_kern_open(int fd, const char* path, int openMode, int perms)
{
	CALLED();

	printf("open %d path %s\n", fd, path);

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	// TODO see BNode::_SetTo

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

#ifdef OPEN_BY_INODE_WORKAROUND
	struct stat st;
	if (_kern_read_stat(fd, path, false, &st, 0) != B_OK) {
		if (!(openMode & O_CREAT))
			return B_ENTRY_NOT_FOUND;
	} else
		BKernelPrivate::insertPath(st.st_ino, fd, path);

	// TODO we should probably be using the following fd to retrieve a path
#endif

	return openat(fd, path, openMode, perms);
}


status_t
_kern_close(int fd)
{
	CALLED();

	return (close(fd) < 0) ? B_ERROR : B_OK;
}


int
_kern_open_dir(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

	int ret = openat(fd, path, O_EXCL);
	if (ret < 0)
		perror("err");

	printf("opendir fd %d path %s ret %d\n", fd, path, ret);

#ifdef OPEN_BY_INODE_WORKAROUND
	struct stat st;
	if (_kern_read_stat(fd, path, false, &st, 0) == B_OK)
		BKernelPrivate::insertPath(st.st_ino, fd, path);
#endif

	return ret < 0 ? B_ENTRY_NOT_FOUND : ret;
}


int
_kern_open_parent_dir(int fd, char* name, size_t length)
{
	CALLED();

	int dirfd = _kern_open_dir(fd, "..");
	if (dirfd < 0)
		return dirfd;

	if (name != NULL) {
		char buf[B_PATH_NAME_LENGTH];
		size_t size = B_PATH_NAME_LENGTH;
		std::string proc = "/proc/self/fd/";
		proc.append(std::to_string(dirfd));

		ssize_t bytes = readlink(proc.c_str(), buf, size);
		if (bytes < 0)
			return B_FILE_ERROR;

		std::string path = std::string(buf, bytes);

		char* baseName = basename(path.c_str());
		if (baseName == NULL) {
			close(dirfd);
			return B_ENTRY_NOT_FOUND;
		}

		size_t len = strlen(baseName);
		if (len > B_PATH_NAME_LENGTH || len > length)
			return B_BUFFER_OVERFLOW;

		if (strlcpy(name, baseName, len) >= len) {
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}
	}

#ifdef OPEN_BY_INODE_WORKAROUND
	struct stat st;
	if (_kern_read_stat(fd, name, false, &st, 0) == B_OK)
		BKernelPrivate::insertPath(st.st_ino, fd, name);
#endif

	return dirfd;
}

int
_kern_open_dir_entry_ref_fd(dev_t device, ino_t node, const char* name)
{
	UNIMPLEMENTED();
}

int
_kern_open_dir_entry_ref(dev_t device, ino_t node, const char* name)
{
	UNIMPLEMENTED();

	std::string path;
	if (BKernelPrivate::getPath(node, name, path) == B_OK) {
		DIR* dir = opendir(path.c_str());
		if (dir != NULL) {
			int result = dup(dirfd(dir));
			closedir(dir);
			return result;
		}
	}

	return B_ERROR;
}


int
_kern_open_entry_ref_fd(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	UNIMPLEMENTED();
}


int
_kern_open_entry_ref(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	UNIMPLEMENTED();

	if (name == NULL || device < 0 || node < 0)
		return B_BAD_VALUE;

	if (name != NULL && name[0] == '\0')
		return B_BAD_VALUE;

	std::string path;
	if (BKernelPrivate::getPath(node, name, path) == B_OK) {
		printf("path %s\n", path.c_str());
		int ret = open(path.c_str(), openMode, perms);

		if (ret < 0) {
			perror(NULL);
			if (errno == EISDIR && openMode & O_RDWR) {
				// TODO openmode, perms
				// TODO inefficient
				return _kern_open_dir_entry_ref(device, node, name);
			} else if (errno == EISDIR) {
				printf("isdir\n");
				return B_ERROR;
			} else if (ret < 0)
				return B_ENTRY_NOT_FOUND;
		}
		printf("ok\n");
		return ret;
	}

	return B_ERROR;
}


status_t
_kern_entry_ref_to_path(dev_t device, ino_t node, const char* leaf,
	char* userPath, size_t pathLength)
{
	UNIMPLEMENTED();

	if (pathLength <= 0)
		return B_BAD_VALUE;
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

	if (userPath == NULL || buffer == NULL)
		return B_BAD_VALUE;

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
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

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

	return (fsync(fd) < 0) ? B_ERROR : B_OK ;
}


status_t
_kern_write_stat(int fd, const char* path, bool traverseLink,
	const struct stat* st, size_t statSize, int statMask)
{
	UNIMPLEMENTED();

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

	return B_OK;
}


ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize,
	uint32 maxCount)
{
	CALLED();
	printf("read dir fd %d\n", fd);
	if (buffer == NULL || bufferSize <= 0 || maxCount <= 0)
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	struct linux_dirent {
		long           d_ino;
		off_t          d_off;
		unsigned short d_reclen;
		char           d_name[256];
	};

	size_t bufSize = sizeof(struct linux_dirent)*(maxCount);
	struct linux_dirent* buf = (linux_dirent*)malloc(bufSize);
	ssize_t ret = syscall(SYS_getdents, fd, buf, bufSize);
	if (ret <= 0) {
		free(buf);
		return B_ENTRY_NOT_FOUND;
	}
	ssize_t retCount = ret/sizeof(struct linux_dirent);

	for (uint32 i = 0; i < retCount; i++) {
		printf("readdir: entry name %s\n", buf[i].d_name);
	}

	for (uint32 i = 0; i < maxCount && i < retCount; i++) {
		buffer[i].d_ino = buf[i].d_ino;
		buffer[i].d_off = buf[i].d_off;
		buffer[i].d_reclen = buf[i].d_reclen;
		strncpy(buffer[i].d_name, buf[i].d_name, 256 - 1);
		buffer[i].d_name[256 - 1] = '\0';
	}

	if (retCount > maxCount)
		lseek(fd, -1*(retCount-maxCount), SEEK_CUR);

	free(buf);
	return (retCount > maxCount) ? maxCount : retCount;
}


off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	CALLED();

	off_t size = lseek(fd, pos, seekType);
	return (size < 0) ? B_ERROR : size;
}


ssize_t
_kern_read(int fd, off_t pos, void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t size = pread(fd, buffer, bufferSize, pos);
	return (size < 0) ? B_ERROR : size;
}


ssize_t
_kern_write(int fd, off_t pos, const void* buffer, size_t bufferSize)
{
	CALLED();

	ssize_t size = pwrite(fd, buffer, bufferSize, pos);
	return (size < 0) ? B_ERROR : size;
}


status_t
_kern_rewind_dir(int fd)
{
	UNIMPLEMENTED();

	// TODO: why not AT_FDCWD
	if (fd < 0)
		return B_BAD_VALUE;

	return _kern_seek(fd, 0, SEEK_SET);
}


status_t
_kern_remove_dir(int fd, const char* path)
{
	CALLED();

	if (path == NULL)
		return B_BAD_VALUE; 

	if (fd < 0)
		fd = AT_FDCWD;

	int ret = unlinkat(fd, path, AT_REMOVEDIR);

	if (ret < 0 && errno == ENOTEMPTY)
		return B_DIRECTORY_NOT_EMPTY;

	return (ret < 0) ? B_ENTRY_NOT_FOUND : B_OK;
}


status_t
_kern_rename(int oldDir, const char* oldPath, int newDir, const char* newPath)
{
	CALLED();

	if (oldPath == NULL || newPath == NULL)
		return B_BAD_VALUE; 

	if (oldDir < 0)
		oldDir = AT_FDCWD;

	if (newDir < 0)
		newDir = AT_FDCWD;

	return (renameat(oldDir, oldPath, newDir, newPath) < 0)
		? B_OK : B_BAD_VALUE;
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

	if (path == NULL)
		return B_BAD_VALUE; 

	if (fd < 0)
		fd = AT_FDCWD;

	if (mkdirat(fd, path, perms) < 0)
		return B_ERROR;

	return B_OK;
}


status_t
_kern_create_symlink(int fd, const char* path,
	const char* toPath, int mode)
{
	CALLED();

	if (path == NULL)
		return B_BAD_VALUE; 

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

	// TODO mode

	return symlinkat(path, fd, toPath);
}


status_t
_kern_unlink(int fd, const char* path)
{
	CALLED();

	if (path == NULL)
		return B_BAD_VALUE; 

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		path = ".";

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
