/*
 *  Copyright 2020-2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <NodeMonitor.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>

#include "KernelDebug.h"


status_t
_kern_read_stat(int fd, const char* path, bool traverseLink,
	struct stat* st, size_t _ignored)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_ERROR;

	int flags = traverseLink == false ? AT_SYMLINK_NOFOLLOW : 0;
	if (path == NULL) {
		if (!traverseLink) {
			if (fstat(fd, st) < 0)
				return B_ENTRY_NOT_FOUND;

			return B_OK;
		}
		flags |= AT_EMPTY_PATH;
	}

	if (fd < 0)
		fd = AT_FDCWD;

	return fstatat(fd, path, st, flags) < 0 ? B_ENTRY_NOT_FOUND : B_OK;
}


int
_kern_open(int fd, const char* path, int openMode, int perms)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	if (path == NULL)
		openMode |= AT_EMPTY_PATH;

	// TODO see BNode::_SetTo
	int ret = openat(fd, path, openMode, perms);
	if (ret < 0)
		return -errno;

	// TODO do we really need this?
	struct stat st;
	if (_kern_read_stat(ret, path, false, &st, 0) != B_OK) {
		if (!(openMode & O_CREAT)) {
			close(ret);
			return B_ENTRY_NOT_FOUND;
		}
	}

	return ret;
}


status_t
_kern_close(int fd)
{
	CALLED();

	return (close(fd) < 0) ? -errno : B_OK;
}


int
_kern_open_dir(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	// TODO O_EXCL only with O_CREAT?
	int flags = O_EXCL | O_DIRECTORY;

	if (!path)
		flags |= AT_EMPTY_PATH;

	int ret = openat(fd, path, flags);
	if (ret < 0)
		return -errno;

	return ret;
}


int
_kern_open_parent_dir(int fd, char* name, size_t length)
{
	CALLED();

	if (fd < 0)
		return B_BAD_VALUE;

	int dirfd = _kern_open_dir(fd, "..");
	if (dirfd < 0)
		return dirfd;

	if (name != NULL) {
		if (length <= 0) {
			close(dirfd);
			return B_BAD_VALUE;
		}

		char buf[B_PATH_NAME_LENGTH];
		status_t ret = _kern_fd_to_path(dirfd, -1, buf, sizeof(buf));
		if (ret != B_OK) {
			close(dirfd);
			return ret;
		}

		// No need to free basename
		char* baseName = basename(buf);
		if (baseName == NULL) {
			close(dirfd);
			return B_ENTRY_NOT_FOUND;
		}

		size_t len = strlen(baseName);
		if (len > B_PATH_NAME_LENGTH || len > length) {
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(name, baseName, len) >= len) {
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}
	}

	return dirfd;
}


int
_kern_open_dir_virtual_ref(vref_id id, const char* name)
{
	int fd = open_vref(id);
	if (fd < 0)
		return fd;

	if (name == NULL || strlen(name) == 0 || (strcmp(name, ".") == 0))
		return fd;

	int ret = openat(fd, name, O_EXCL | O_DIRECTORY);
	close(fd);
	return ret;
}


int
_kern_open_dir_entry_ref(dev_t device, ino_t node, const char* name)
{
	CALLED();

	if (device <= 0 || node <= 0)
		return B_BAD_VALUE;

	if (device == get_vref_dev())
		return _kern_open_dir_virtual_ref(node, name);

	UNIMPLEMENTED();

	return B_ERROR;
}


int
_kern_open_virtual_ref(vref_id id, const char* name,
	int openMode, int perms)
{
	int fd = open_vref(id);
	if (fd < 0)
		return fd;

	if (name == NULL || strlen(name) == 0 || (strcmp(name, ".") == 0))
		return fd;

	int ret = openat(fd, name, openMode, perms);
	close(fd);
	return ret;
}


int
_kern_open_entry_ref(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	CALLED();

	printf("open entry ref %lu %lu vref dev is %lu\n", device, node, get_vref_dev());

	if (device <= 0 || node <= 0)
		return B_BAD_VALUE;

	if (device == get_vref_dev())
		return _kern_open_virtual_ref(node, name, openMode, perms);

	UNIMPLEMENTED();

	return B_ERROR;
}


status_t
_kern_entry_ref_to_path(dev_t device, ino_t node, const char* leaf,
	char* userPath, size_t pathLength)
{
	CALLED();

	printf("entry ref to path %d %d %s\n", device, node, leaf);

	if (leaf == NULL && (device < 0 || node < 0))
		return B_BAD_VALUE;

	if ((leaf != NULL && leaf[0] == '\0') || pathLength <= 0)
		return B_BAD_VALUE;

	if (leaf != NULL && leaf[0] == '/') {
		if (strlcpy(userPath, leaf,
				strlen(leaf)) >= pathLength) {
			return B_BUFFER_OVERFLOW;
		}
		return B_OK;
	}

	if (device == get_vref_dev()) {
		vref_id id = (vref_id) node;
		int fd = open_vref(id);
		if (fd < 0)
			return B_ERROR;

		status_t ret = _kern_entry_ref_to_path_by_fd(fd, -1, leaf,
			userPath, pathLength);
		close(fd);
		return ret;
	}

	UNIMPLEMENTED();

	return B_ERROR;
}


// TODO remove?
int
_kern_open_dir_entry_ref_fd(dev_t device, ino_t node, const char* name)
{
	return _kern_open_dir_entry_ref(device, node, name);
}


int
_kern_open_entry_ref_fd(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	return _kern_open_entry_ref(device, node, name, openMode, perms);
}


status_t
_kern_entry_ref_to_path_by_fd(int fd, team_id team, char* name,
	char* buffer, size_t bufferSize)
{
	if (name == NULL)
		return _kern_fd_to_path(fd, team, buffer, bufferSize);

	if (name != NULL && name[0] == '/') {
		if (strlcpy(buffer, name, strlen(name)) >= bufferSize)
			return B_BUFFER_OVERFLOW;
	
		return B_OK;
	}

	char resolvedPath[B_PATH_NAME_LENGTH];
	status_t ret = _kern_fd_to_path(fd, team,
		resolvedPath, sizeof(resolvedPath));

	if (ret != B_OK)
		return ret;

	// TODO stat?
	snprintf(buffer, bufferSize, "%s/%s", resolvedPath, name);
	return B_OK;
}

status_t
_kern_fd_to_path(int fd, team_id team, char* buffer, size_t bufferSize)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	if (buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;

	char procPath[B_PATH_NAME_LENGTH];
	if (team == -1 || team == getpid())
		snprintf(procPath, sizeof(procPath), "/proc/self/fd/%d", fd);
	else
		snprintf(procPath, sizeof(procPath), "/proc/%d/fd/%d", team, fd);

	ssize_t size = readlink(procPath, buffer, bufferSize);
	if (size < 0)
		return -errno;

	buffer[size] = '\0';

	return B_OK;
}


status_t
_kern_read_link(int fd, const char* path, char* buffer, size_t* _bufferSize)
{
	CALLED();

	if (_bufferSize == NULL || buffer == NULL)
		return B_BAD_VALUE;

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	// Since Linux 2.6.39, path can be an empty string
	if (path == NULL)
		path = "";

	ssize_t size = readlinkat(fd, path, buffer, *_bufferSize);
	if (size < 0)
		return -errno;

	*_bufferSize = size;
	return B_OK;
}


status_t
_kern_fsync(int fd)
{
	CALLED();
	
	if (fd < 0)
		return B_FILE_ERROR;

	return (fsync(fd) < 0) ? -errno : B_OK ;
}


status_t
_kern_write_stat(int fd, const char* path, bool traverseLink,
	const struct stat* st, size_t statSize, int statMask)
{
	CALLED();

	if (st == NULL || statSize != sizeof(struct stat)
			|| (fd < 0 && path == NULL))
		return B_BAD_VALUE;

	if (fd < 0)
		fd = AT_FDCWD;

	int flags = traverseLink ? 0 : AT_SYMLINK_NOFOLLOW;

	if (path == NULL)
		flags |= AT_EMPTY_PATH;

	if (statMask & B_STAT_MODE) {
		if (fchmodat(fd, path, st->st_mode, flags) < 0)
			return -errno;
	}

	if (statMask & B_STAT_UID) {
		if (fchownat(fd, path, st->st_uid, -1, flags) < 0)
			return -errno;
	}

	if (statMask & B_STAT_GID) {
		if (fchownat(fd, path, -1, st->st_gid, flags) < 0)
			return -errno;
	}

	if (statMask & B_STAT_SIZE && S_ISREG(st->st_mode)) {
		if (ftruncate64(fd, st->st_size) < 0)
			return -errno;
	}

	if (statMask & (B_STAT_MODIFICATION_TIME | B_STAT_ACCESS_TIME)) {
		struct timespec times[2];
		memset(times, 0, sizeof(times));

		if (statMask & B_STAT_ACCESS_TIME)
			times[0].tv_sec = st->st_atime;

		if (statMask & B_STAT_MODIFICATION_TIME)
			times[1].tv_sec = st->st_mtime;

		if (utimensat(fd, path, times, 0) < 0)
			return -errno;
	}

	return B_OK;
}


ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize, uint32 maxCount)
{
	CALLED();

	if (buffer == NULL || bufferSize <= 0 || maxCount == 0)
		return B_BAD_VALUE;

	if (fd < 0)
		return B_FILE_ERROR;

	struct linux_dirent {
		long           d_ino;
		off_t          d_off;
		unsigned short d_reclen;
		char           d_name[255];
	};

	size_t bufSize = sizeof(struct linux_dirent) * maxCount;
	struct linux_dirent* direntBuffer = malloc(bufSize);
	if (direntBuffer == NULL)
		return B_NO_MEMORY;

	off_t seekOffset = _kern_seek(fd, 0, SEEK_CUR);

	ssize_t ret = syscall(SYS_getdents, fd, direntBuffer, bufSize);
	if (ret < 0) {
		free(direntBuffer);
		return -errno;
	}

	int i = 0;
	int pos = 0;
	while (pos < ret && i < maxCount) {
		struct linux_dirent* dir = (struct linux_dirent *) (direntBuffer + pos);
		buffer[i].d_ino = dir->d_ino;
		buffer[i].d_off = dir->d_off;
		buffer[i].d_reclen = dir->d_reclen;

		if (strlcpy(buffer[i].d_name, dir->d_name, sizeof(buffer[i].d_name))
				> sizeof(buffer[i].d_name)) {
			free(direntBuffer);
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}

        pos += dir->d_reclen;
        seekOffset = dir->d_off;
        i++;
	}
	free(direntBuffer);

	if (pos < ret)
		_kern_seek(fd, seekOffset, SEEK_SET);

	if (i <= 0)
		return B_ENTRY_NOT_FOUND;

	return i;
}


off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	off_t size = lseek(fd, pos, seekType);
	return (size < 0) ? -errno : size;
}


ssize_t
_kern_read(int fd, off_t pos, void* buffer, size_t bufferSize)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	ssize_t size = pread(fd, buffer, bufferSize, pos);
	return (size < 0) ? -errno : size;
}


ssize_t
_kern_write(int fd, off_t pos, const void* buffer, size_t bufferSize)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	ssize_t size = pwrite(fd, buffer, bufferSize, pos);
	return (size < 0) ? -errno : size;
}


status_t
_kern_rewind_dir(int fd)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	return _kern_seek(fd, 0, SEEK_SET);
}


status_t
_kern_remove_dir(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE; 

	if (fd < 0)
		fd = AT_FDCWD;

	int flags = AT_REMOVEDIR;

	if (path == NULL)
		flags |= AT_EMPTY_PATH;

	if (unlinkat(fd, path, flags) < 0)
		return -errno;

	return B_OK;
}


status_t
_kern_rename(int oldDir, const char* oldPath,
	int newDir, const char* newPath)
{
	CALLED();

	if ((oldDir < 0 && oldPath == NULL)
			|| (newDir < 0 && newPath == NULL)) {
		return B_BAD_VALUE;
	}

	if (oldDir < 0)
		oldDir = AT_FDCWD;

	if (newDir < 0)
		newDir = AT_FDCWD;

	return (renameat(oldDir, oldPath, newDir, newPath) < 0) ? -errno : B_OK;
}


int
_kern_dup(int fd)
{
	CALLED();
	if (fd < 0)
		return B_FILE_ERROR;

	int ret = dup(fd);
	return (ret < 0) ? -errno : ret;
}


status_t
_kern_create_dir(int fd, const char* path, int perms)
{
	CALLED();

	if (path == NULL)
		return B_BAD_VALUE;

	return (mkdirat((fd < 0 ? AT_FDCWD : fd), path, perms) < 0) ? -errno : B_OK;
}


status_t
_kern_create_symlink(int fd, const char* path,
	const char* toPath, int mode)
{
	CALLED();

	// symlinkat does not seem to support AT_EMPTY_PATH
	if (path == NULL || toPath == NULL)
		return B_BAD_VALUE; 

	// TODO mode?

	return (symlinkat(path, (fd < 0 ? AT_FDCWD : fd), toPath) < 0) ? -errno : B_OK;
}


status_t
_kern_unlink(int fd, const char* path)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_FILE_ERROR;

	if (fd < 0)
		fd = AT_FDCWD;

	int flags = (path == NULL ? AT_EMPTY_PATH : 0);
	return (unlinkat(fd, path, flags | AT_SYMLINK_NOFOLLOW) < 0) ? -errno : B_OK;
}


status_t
_kern_lock_node(int fd)
{
	if (fd < 0)
		return B_FILE_ERROR;

	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) == -1)
		return -errno;

	return B_OK;
}


status_t
_kern_unlock_node(int fd)
{
	if (fd < 0)
		return B_FILE_ERROR;

	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	if (fcntl(fd, F_SETLK, &lock) == -1)
		return -errno;

	return B_OK;
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
