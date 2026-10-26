/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Attribute syscall wrappers — all operations are kernel-mediated via
 * NEXUS_ATTR_* ioctls on the node_monitor fd.  No userland xattr calls,
 * no global fake-fd map.
 */

#include <syscalls.h>
#include <fs_attr.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "KernelDebug.h"
#include "Team.h"

#include "../../kernel/nexus/nexus/nexus.h"


extern "C" int
_kern_open_attr_dir(int fd, const char *path, bool traverseLeafLink)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	int targetFd = fd;
	bool needClose = false;

	if (path != NULL) {
		int flags = O_RDONLY | O_CLOEXEC;
		if (!traverseLeafLink)
			flags |= O_NOFOLLOW;
		targetFd = open(path, flags);
		if (targetFd < 0)
			return B_ENTRY_NOT_FOUND;
		needClose = true;
	}

	struct nexus_attr_dir_open req;
	req.target_fd = targetFd;
	req.flags = 0;

	int ret = ioctl(nmfd, NEXUS_ATTR_DIR_OPEN, &req);

	if (needClose)
		close(targetFd);

	if (ret < 0)
		return -errno;

	return ret;
}


extern "C" int fs_fopen_attr(int fd, const char *attribute, uint32 type,
	int openMode);


extern "C" int
_kern_open_attr(int fd, const char *path, const char *name,
		uint32 type, int openMode)
{
	if (path == NULL)
		return fs_fopen_attr(fd, name, type, openMode);

	int nodeFd = openat(fd < 0 ? AT_FDCWD : fd, path, O_RDONLY | O_CLOEXEC);
	if (nodeFd < 0)
		return B_ENTRY_NOT_FOUND;

	int result = fs_fopen_attr(nodeFd, name, type, openMode);
	close(nodeFd);
	return result;
}


extern "C" ssize_t
fs_read_attr(int fd, const char *attribute, uint32 type,
		off_t pos, void *buffer, size_t readBytes)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;
	if (attribute == NULL || buffer == NULL)
		return B_BAD_VALUE;

	size_t nameLen = strlen(attribute);
	if (nameLen > NEXUS_ATTR_NAME_MAX)
		return B_NAME_TOO_LONG;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	struct nexus_attr_io req;
	memset(&req, 0, sizeof(req));
	req.target_fd = fd;
	memcpy(req.name, attribute, nameLen + 1);
	req.type = 0;
	req.pos = pos;
	req.buf_addr = (uint64_t)(uintptr_t)buffer;
	req.buf_len = (uint64_t)readBytes;

	int ret = ioctl(nmfd, NEXUS_ATTR_READ, &req);
	if (ret < 0) {
		if (errno == ENODATA)
			return B_ENTRY_NOT_FOUND;
		if (errno == ERANGE)
			return B_BUFFER_OVERFLOW;
		return -errno;
	}

	return (ssize_t)ret;
}


extern "C" ssize_t
fs_write_attr(int fd, const char *attribute, uint32 type,
		off_t pos, const void *buffer, size_t writeBytes)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;
	if (attribute == NULL || (buffer == NULL && writeBytes > 0))
		return B_BAD_VALUE;

	size_t nameLen = strlen(attribute);
	if (nameLen > NEXUS_ATTR_NAME_MAX)
		return B_NAME_TOO_LONG;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	struct nexus_attr_io req;
	memset(&req, 0, sizeof(req));
	req.target_fd = fd;
	memcpy(req.name, attribute, nameLen + 1);
	req.type = type;
	req.pos = pos;
	req.buf_addr = (uint64_t)(uintptr_t)buffer;
	req.buf_len = (uint64_t)writeBytes;

	int ret = ioctl(nmfd, NEXUS_ATTR_WRITE, &req);
	if (ret < 0) {
		if (errno == ENODATA)
			return B_ENTRY_NOT_FOUND;
		return -errno;
	}

	return (ssize_t)ret;
}


extern "C" status_t
fs_stat_attr(int fd, const char *attribute, struct attr_info *attrInfo)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;
	if (attribute == NULL)
		return B_BAD_VALUE;

	size_t nameLen = strlen(attribute);
	if (nameLen > NEXUS_ATTR_NAME_MAX)
		return B_NAME_TOO_LONG;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	struct nexus_attr_stat req;
	memset(&req, 0, sizeof(req));
	req.target_fd = fd;
	memcpy(req.name, attribute, nameLen + 1);

	int ret = ioctl(nmfd, NEXUS_ATTR_STAT, &req);
	if (ret < 0) {
		if (errno == ENODATA)
			return B_ENTRY_NOT_FOUND;
		return -errno;
	}

	if (attrInfo != NULL) {
		attrInfo->type = req.type_out;
		attrInfo->size = (off_t)req.size_out;
	}

	return B_OK;
}


extern "C" status_t
_kern_remove_attr(int fd, const char *attribute)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;
	if (attribute == NULL)
		return B_BAD_VALUE;

	size_t nameLen = strlen(attribute);
	if (nameLen > NEXUS_ATTR_NAME_MAX)
		return B_NAME_TOO_LONG;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	struct nexus_attr_remove req;
	memset(&req, 0, sizeof(req));
	req.target_fd = fd;
	memcpy(req.name, attribute, nameLen + 1);

	int ret = ioctl(nmfd, NEXUS_ATTR_REMOVE, &req);
	if (ret < 0) {
		if (errno == ENODATA)
			return B_ENTRY_NOT_FOUND;
		return -errno;
	}

	return B_OK;
}


extern "C" status_t
fs_remove_attr(int fd, const char *name)
{
	return _kern_remove_attr(fd, name);
}


extern "C" status_t
_kern_rename_attr(int fromFile, const char *fromName,
		int toFile, const char *toName)
{
	CALLED();

	if (fromFile < 0 || toFile < 0)
		return B_FILE_ERROR;
	if (fromName == NULL || toName == NULL)
		return B_BAD_VALUE;

	if (strlen(fromName) > NEXUS_ATTR_NAME_MAX
			|| strlen(toName) > NEXUS_ATTR_NAME_MAX)
		return B_NAME_TOO_LONG;

	int nmfd = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nmfd < 0)
		return B_NO_INIT;

	struct nexus_attr_rename req;
	memset(&req, 0, sizeof(req));
	req.from_fd = fromFile;
	req.to_fd = toFile;
	strncpy(req.from_name, fromName, sizeof(req.from_name) - 1);
	strncpy(req.to_name, toName, sizeof(req.to_name) - 1);

	int ret = ioctl(nmfd, NEXUS_ATTR_RENAME, &req);
	if (ret < 0) {
		if (errno == ENODATA)
			return B_ENTRY_NOT_FOUND;
		return -errno;
	}

	return B_OK;
}


extern "C" status_t
fs_rename_attr(int fromFile, const char *fromName,
		int toFile, const char *toName)
{
	return _kern_rename_attr(fromFile, fromName, toFile, toName);
}


extern "C" DIR *
fs_open_attr_dir(const char *path)
{
	if (path == NULL)
		return NULL;

	int pathFd = open(path, O_RDONLY | O_CLOEXEC);
	if (pathFd < 0)
		return NULL;

	int attrFd = _kern_open_attr_dir(pathFd, NULL, true);
	close(pathFd);

	if (attrFd < 0)
		return NULL;

	DIR *dir = fdopendir(attrFd);
	if (dir == NULL)
		close(attrFd);

	return dir;
}


extern "C" DIR *
fs_fopen_attr_dir(int fd)
{
	int attrFd = _kern_open_attr_dir(fd, NULL, true);
	if (attrFd < 0)
		return NULL;

	DIR *dir = fdopendir(attrFd);
	if (dir == NULL)
		close(attrFd);

	return dir;
}


extern "C" struct dirent *
fs_read_attr_dir(DIR *dir)
{
	if (dir == NULL)
		return NULL;

	return readdir(dir);
}


extern "C" int
fs_close_attr_dir(DIR *dir)
{
	if (dir == NULL)
		return B_BAD_VALUE;

	return closedir(dir);
}


struct OpenAttribute {
	int		fd;
	int		nodeFd;
	char	name[NEXUS_ATTR_NAME_MAX + 1];
	uint32	type;
	bool	writable;
};

static const int kMaxOpenAttributes = 256;
static OpenAttribute sOpenAttributes[kMaxOpenAttributes];
static int sOpenAttributeCount = 0;
static pthread_mutex_t sOpenAttributeLock = PTHREAD_MUTEX_INITIALIZER;


extern "C" int
fs_fopen_attr(int fd, const char *attribute, uint32 type, int openMode)
{
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;
	if (attribute == NULL || strlen(attribute) > NEXUS_ATTR_NAME_MAX)
		return B_BAD_VALUE;

	struct attr_info info;
	bool exists = fs_stat_attr(fd, attribute, &info) == B_OK;
	if (!exists && (openMode & O_CREAT) == 0)
		return B_ENTRY_NOT_FOUND;

	int accMode = openMode & O_ACCMODE;
	bool writable = accMode == O_WRONLY || accMode == O_RDWR;
	bool truncate = exists && (openMode & O_TRUNC) != 0 && writable;
	if (exists && type == 0)
		type = info.type;

	int memFd = memfd_create(attribute, MFD_CLOEXEC);
	if (memFd < 0)
		return -errno;

	if (exists && !truncate && info.size > 0) {
		char* buffer = (char*)malloc(info.size);
		if (buffer == NULL) {
			close(memFd);
			return B_NO_MEMORY;
		}
		ssize_t got = fs_read_attr(fd, attribute, info.type, 0, buffer,
			info.size);
		if (got < 0 || write(memFd, buffer, got) != got) {
			free(buffer);
			close(memFd);
			return got < 0 ? (int)got : B_IO_ERROR;
		}
		free(buffer);
		lseek(memFd, 0, SEEK_SET);
	}

	int nodeFd = dup(fd);
	if (nodeFd < 0) {
		close(memFd);
		return -errno;
	}

	pthread_mutex_lock(&sOpenAttributeLock);
	if (sOpenAttributeCount >= kMaxOpenAttributes) {
		pthread_mutex_unlock(&sOpenAttributeLock);
		close(nodeFd);
		close(memFd);
		return B_NO_MEMORY;
	}
	OpenAttribute& entry = sOpenAttributes[sOpenAttributeCount++];
	entry.fd = memFd;
	entry.nodeFd = nodeFd;
	strcpy(entry.name, attribute);
	entry.type = type;
	entry.writable = writable;
	pthread_mutex_unlock(&sOpenAttributeLock);

	if (!exists || truncate) {
		fs_write_attr(fd, attribute, type, 0, "", 0);
	}

	return memFd;
}


extern "C" int
fs_open_attr(const char *path, const char *attribute,
		uint32 type, int openMode)
{
	if (path == NULL)
		return B_BAD_VALUE;

	int nodeFd = open(path, O_RDONLY | O_CLOEXEC);
	if (nodeFd < 0)
		return B_ENTRY_NOT_FOUND;

	int result = fs_fopen_attr(nodeFd, attribute, type, openMode);
	close(nodeFd);
	return result;
}


extern "C" int
fs_close_attr(int fd)
{
	CALLED();

	OpenAttribute entry;
	bool found = false;

	pthread_mutex_lock(&sOpenAttributeLock);
	for (int i = 0; i < sOpenAttributeCount; i++) {
		if (sOpenAttributes[i].fd != fd)
			continue;
		entry = sOpenAttributes[i];
		sOpenAttributes[i] = sOpenAttributes[--sOpenAttributeCount];
		found = true;
		break;
	}
	pthread_mutex_unlock(&sOpenAttributeLock);

	if (!found)
		return B_FILE_ERROR;

	status_t status = B_OK;
	if (entry.writable) {
		struct stat st;
		if (fstat(fd, &st) != 0) {
			status = -errno;
		} else {
			char* buffer = (char*)malloc(st.st_size > 0 ? st.st_size : 1);
			if (buffer == NULL) {
				status = B_NO_MEMORY;
			} else {
				ssize_t got = pread(fd, buffer, st.st_size, 0);
				if (got < 0) {
					status = -errno;
				} else {
					fs_remove_attr(entry.nodeFd, entry.name);
					ssize_t written = fs_write_attr(entry.nodeFd, entry.name,
						entry.type, 0, buffer, got);
					if (written < 0)
						status = (status_t)written;
				}
				free(buffer);
			}
		}
	}

	close(entry.nodeFd);
	close(fd);
	return status;
}
