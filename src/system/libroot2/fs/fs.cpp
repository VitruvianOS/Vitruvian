/*
 *  Copyright 2020-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <NodeMonitor.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>

#include "LinuxVolume.h"
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

	int ret;
	if (openMode & O_CREAT)
		ret = openat(fd, path, openMode, perms);
	else
		ret = openat(fd, path, openMode);

	if (ret < 0)
		return -errno;

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

	int flags = O_DIRECTORY;

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
		return -errno;

	if (name != NULL) {
		if (length <= 0) {
			close(dirfd);
			return B_BAD_VALUE;
		}

		char buf[B_PATH_NAME_LENGTH];
		status_t ret = _kern_fd_to_path(fd, -1, buf, sizeof(buf));
		if (ret != B_OK) {
			close(dirfd);
			return ret;
		}

		char* baseName = basename(buf);
		if (baseName == NULL) {
			close(dirfd);
			return B_ENTRY_NOT_FOUND;
		}

		if (strcmp(baseName, "/") == 0)
			baseName = (char*)".";

		size_t len = strlen(baseName);
		if (len+1 > length) {
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}

		if (strlcpy(name, baseName, length) >= length) {
			close(dirfd);
			return B_BUFFER_OVERFLOW;
		}
	}

	return dirfd;
}


int
_kern_open_dir_virtual_ref(vref_id id, const char* name)
{
	if (id < 0)
		return B_BAD_VALUE;

	int fd = open_vref(id);
	if (fd < 0)
		return fd;

	if (name == NULL || strlen(name) == 0 || (strcmp(name, ".") == 0))
		return fd;

	int ret = openat(fd, name, O_DIRECTORY | O_CLOEXEC);
	close(fd);
	return (ret < 0) ? -errno : ret;
}


int
_kern_open_dir_entry_ref(dev_t device, ino_t node, const char* name)
{
	CALLED();

	if (device == B_INVALID_DEV || node == B_INVALID_INO)
		return B_BAD_VALUE;

	if (device == get_vref_dev())
		return _kern_open_dir_virtual_ref(node, name);

	char path[B_PATH_NAME_LENGTH];
	status_t ret = _kern_entry_ref_to_path(device, node, name, path,
		B_PATH_NAME_LENGTH);

	if (ret != B_OK) {
		// TODO stack trace here
		UNIMPLEMENTED();
		return ret;
	}

	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	return (fd < 0) ? -errno : fd;
}


int
_kern_open_virtual_ref(vref_id id, const char* name,
	int openMode, int perms)
{
	if (id < 0)
		return B_BAD_VALUE;

	int fd = open_vref(id);
	if (fd < 0)
		return fd;

	if (name == NULL || strlen(name) == 0 || (strcmp(name, ".") == 0))
		return fd;

	int retFd = openat(fd, name, openMode, perms);
	close(fd);
	return (retFd < 0) ? -errno : retFd;
}


int
_kern_open_entry_ref(dev_t device, ino_t node, const char* name,
	int openMode, int perms)
{
	CALLED();

	printf("open entry ref %llu %llu vref dev is %llu\n",
		(unsigned long long)device, (unsigned long long)node,
		(unsigned long long)get_vref_dev());

	if (device == B_INVALID_DEV || node == B_INVALID_INO)
		return B_BAD_VALUE;

	if (device == get_vref_dev())
		return _kern_open_virtual_ref(node, name, openMode, perms);

	char path[B_PATH_NAME_LENGTH];
	status_t ret = _kern_entry_ref_to_path(device, node, name, path,
		B_PATH_NAME_LENGTH);

	if (ret != B_OK)
		return ret;

	int fd = open(path, openMode, perms);
	return (fd < 0) ? -errno : fd;
}


status_t
_kern_entry_ref_to_path(dev_t device, ino_t node, const char* leaf,
	char* userPath, size_t pathLength)
{
	CALLED();

	printf("entry ref to path %llu %llu %s\n",
		(unsigned long long)device, (unsigned long long)node,
		(leaf ? leaf : "(null)"));

	if (leaf == NULL && (device == B_INVALID_DEV || node == B_INVALID_INO))
		return B_BAD_VALUE;

	if ((leaf != NULL && leaf[0] == '\0') || pathLength == 0)
		return B_BAD_VALUE;

	if (leaf != NULL && leaf[0] == '/') {
		if (strlcpy(userPath, leaf, pathLength) >= pathLength) {
			return B_BUFFER_OVERFLOW;
		}
		return B_OK;
	}

	if (device == get_vref_dev()) {
		vref_id id = (vref_id) node;
		if (id < 0)
			return B_ENTRY_NOT_FOUND;

		int fd = open_vref(id);
		if (fd < 0)
			return fd;

		status_t ret = _kern_entry_ref_to_path_by_fd(fd, -1, leaf,
			userPath, pathLength);

		close(fd);
		return ret;
	}

	// This is an exception to support opening volumes. If the dev_t, ino_t
	// and leaf match, then we open the entry ref.
	struct mntent* mountEntry = BKernelPrivate::LinuxVolume::FindVolume(device);
	if (!mountEntry) {
		return B_ENTRY_NOT_FOUND;
	}

	struct stat st;
	if (stat(mountEntry->mnt_dir, &st) < 0) {
		BKernelPrivate::LinuxVolume::FreeVolumeEntry(mountEntry);
		return -errno;
	}

	if (node == st.st_ino) {
		printf("fs: opening dev_t %lld %s\n", st.st_dev, mountEntry->mnt_dir);
		if (snprintf(userPath, pathLength, "%s/%s",
				mountEntry->mnt_dir, leaf) >= pathLength) {
			BKernelPrivate::LinuxVolume::FreeVolumeEntry(mountEntry);
			return B_BUFFER_OVERFLOW;
		}
		BKernelPrivate::LinuxVolume::FreeVolumeEntry(mountEntry);
		return B_OK;
	}

	BKernelPrivate::LinuxVolume::FreeVolumeEntry(mountEntry);

	// TODO backtrace here

	UNIMPLEMENTED();

	return B_ENTRY_NOT_FOUND;
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
		if (strlcpy(buffer, name, bufferSize) >= bufferSize)
			return B_BUFFER_OVERFLOW;

		return B_OK;
	}

	char resolvedPath[B_PATH_NAME_LENGTH];
	status_t ret = _kern_fd_to_path(fd, team,
		resolvedPath, sizeof(resolvedPath));

	if (ret != B_OK)
		return ret;

	if (snprintf(buffer, bufferSize, "%s/%s", resolvedPath, name) >= (int)bufferSize)
		return B_BUFFER_OVERFLOW;

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

	ssize_t size = readlink(procPath, buffer, bufferSize - 1);
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

	// Since Linux 2.6.39, path can be an empty string; allow path==NULL => ""
	if (path == NULL)
		path = "";

	if (*_bufferSize == 0)
		return B_BAD_VALUE;

	ssize_t size = readlinkat(fd, path, buffer, *_bufferSize - 1);
	if (size < 0)
		return -errno;

	buffer[size] = '\0';
	*_bufferSize = (size_t)size;
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
		times[0].tv_nsec = UTIME_OMIT;
		times[1].tv_nsec = UTIME_OMIT;

		if (statMask & B_STAT_ACCESS_TIME) {
			times[0].tv_sec = st->st_atime;
			times[0].tv_nsec = 0;
		} else {
			times[0].tv_sec = 0;
		}

		if (statMask & B_STAT_MODIFICATION_TIME) {
			times[1].tv_sec = st->st_mtime;
			times[1].tv_nsec = 0;
		} else {
			times[1].tv_sec = 0;
		}

		if (utimensat(fd, path, times, flags) < 0)
			return -errno;
	}

	return B_OK;
}


ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize, uint32 maxCount)
{
	CALLED();

	if (buffer == NULL || bufferSize == 0 || maxCount == 0)
		return B_BAD_VALUE;

	if (fd < 0)
		return B_FILE_ERROR;

	// linux_dirent64 header (LP64): d_ino(8) + d_off(8) + d_reclen(2) + d_type(1) = 19 bytes
	const size_t LINUX_DIRENT64_HEADER = 8 + 8 + 2 + 1;

	if (NAME_MAX == 0)
		return B_BAD_VALUE;
	if (maxCount > SIZE_MAX / (LINUX_DIRENT64_HEADER + NAME_MAX))
		return B_BAD_VALUE;
	size_t bufSize = maxCount * (LINUX_DIRENT64_HEADER + NAME_MAX);

	void* direntBuffer = malloc(bufSize);
	if (direntBuffer == NULL)
		return B_NO_MEMORY;

	off_t seekOffset = _kern_seek(fd, 0, SEEK_CUR);
	if (seekOffset < 0)
		return seekOffset;

	ssize_t ret = syscall(SYS_getdents64, fd, direntBuffer, bufSize);
	if (ret < 0) {
		int saved_errno = errno;
		free(direntBuffer);
		return -saved_errno;
	}

	int i = 0;
	size_t pos = 0;
	while (pos < (size_t)ret && i < (int)maxCount) {
		if ((size_t)ret - pos < LINUX_DIRENT64_HEADER)
			break;

		char* base = (char*)direntBuffer + pos;
		uint64_t d_ino = *(uint64_t*)(base + 0);
		int64_t  d_off = *(int64_t*)(base + 8);
		uint16_t reclen = *(uint16_t*)(base + 16);

		if (reclen < LINUX_DIRENT64_HEADER)
			break;
		if (pos + reclen > (size_t)ret)
			break;

		buffer[i].d_ino = (ino_t)d_ino;
		buffer[i].d_off = (off_t)d_off;
		buffer[i].d_reclen = reclen;

		size_t name_len = reclen - LINUX_DIRENT64_HEADER;
		size_t dst_size = sizeof(buffer[i].d_name);
		size_t to_copy = name_len;
		if (to_copy >= dst_size)
			return B_BUFFER_OVERFLOW;
		memcpy(buffer[i].d_name, base + LINUX_DIRENT64_HEADER, to_copy);
		buffer[i].d_name[to_copy] = '\0';

		pos += reclen;
		seekOffset = d_off;
		i++;
	}

	free(direntBuffer);

	if (pos < (size_t)ret) {
		off_t offset = _kern_seek(fd, seekOffset, SEEK_SET);
		if (offset < 0)
			return offset;
	}

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

	if (path == NULL || toPath == NULL)
		return B_BAD_VALUE;

	return (symlinkat(toPath, (fd < 0 ? AT_FDCWD : fd), path) < 0) ? -errno : B_OK;
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
