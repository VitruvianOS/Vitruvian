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


extern "C" int
_kern_open_attr(int fd, const char *path, const char *name,
		uint32 type, int openMode)
{
	/* Legacy entry point — no ioctl equivalent. */
	return B_NOT_SUPPORTED;
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


extern "C" int
fs_fopen_attr(int fd, const char *attribute, uint32 type, int openMode)
{
	return B_NOT_SUPPORTED;
}


extern "C" int
fs_open_attr(const char *path, const char *attribute,
		uint32 type, int openMode)
{
	return B_NOT_SUPPORTED;
}


extern "C" int
fs_close_attr(int fd)
{
	return B_NOT_SUPPORTED;
}
