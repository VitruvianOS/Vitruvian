/*
 * Copyright 2019-2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <fs_attr.h>

#include <sys/xattr.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <map>
#include <string>
#include <vector>

#include "KernelDebug.h"

// Structure to hold attribute directory state
struct attr_dir {
	int fd;
	std::vector<std::string> attrNames;
	size_t currentIndex;
	struct dirent currentDirent;
};

// Map to store attribute directories by file descriptor
static std::map<int, attr_dir*> sAttrDirs;
static int sNextAttrDirFd = 10000;  // Start high to avoid conflicts
static pthread_mutex_t sAttrDirMutex = PTHREAD_MUTEX_INITIALIZER;

static DIR*
open_attr_dir(int fd, const char* path,
		bool traverseLeafLink) {
	UNIMPLEMENTED();
	return NULL;
}


extern "C" DIR*
fs_open_attr_dir(const char* path)
{
	UNIMPLEMENTED();
	return NULL;
}


extern "C" DIR*
fs_fopen_attr_dir(int fd)
{
	UNIMPLEMENTED();
	return NULL;
}


extern "C" struct dirent*
fs_read_attr_dir(DIR* dir)
{
	UNIMPLEMENTED();
	return NULL;
}


extern int
_kern_open_attr_dir(int fd, const char *path, bool traverseLeafLink)
{
	CALLED();

	if (fd < 0 && path == NULL)
		return B_BAD_VALUE;

	int targetFd = fd;
	bool needClose = false;

	// If path is provided, open the file
	if (path != NULL) {
		int flags = O_RDONLY;
		if (!traverseLeafLink)
			flags |= O_NOFOLLOW;
		targetFd = open(path, flags);
		if (targetFd < 0)
			return B_ENTRY_NOT_FOUND;
		needClose = true;
	}

	// List all extended attributes
	ssize_t listSize = flistxattr(targetFd, NULL, 0);
	if (listSize < 0) {
		if (needClose)
			close(targetFd);
		return B_ERROR;
	}

	char* attrList = NULL;
	if (listSize > 0) {
		attrList = (char*)malloc(listSize);
		if (!attrList) {
			if (needClose)
				close(targetFd);
			return B_NO_MEMORY;
		}

		ssize_t actualSize = flistxattr(targetFd, attrList, listSize);
		if (actualSize < 0) {
			free(attrList);
			if (needClose)
				close(targetFd);
			return B_ERROR;
		}
		listSize = actualSize;
	}

	// Create attribute directory structure
	attr_dir* attrDir = new attr_dir;
	attrDir->fd = needClose ? targetFd : dup(targetFd);
	attrDir->currentIndex = 0;

	// Parse attribute names (null-separated list) and filter for "user.beos." prefix
	if (listSize > 0) {
		char* p = attrList;
		char* end = attrList + listSize;
		while (p < end) {
			const char* prefix = "user.beos.";
			size_t prefixLen = strlen(prefix);
			if (strncmp(p, prefix, prefixLen) == 0) {
				// Strip "user.beos." prefix to get BeOS attribute name
				attrDir->attrNames.push_back(std::string(p + prefixLen));
			}
			p += strlen(p) + 1;
		}
		free(attrList);
	}

	// Assign a file descriptor for this attribute directory
	pthread_mutex_lock(&sAttrDirMutex);
	int attrDirFd = sNextAttrDirFd++;
	sAttrDirs[attrDirFd] = attrDir;
	pthread_mutex_unlock(&sAttrDirMutex);

	return attrDirFd;
}


extern "C" int
fs_close_attr_dir(DIR* dir)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


// Helper functions for integrating with _kern_read_dir / _kern_close / _kern_rewind_dir

bool
_is_attr_dir_fd(int fd)
{
	pthread_mutex_lock(&sAttrDirMutex);
	bool result = (sAttrDirs.find(fd) != sAttrDirs.end());
	pthread_mutex_unlock(&sAttrDirMutex);
	return result;
}


ssize_t
_read_attr_dir(int fd, struct dirent* buffer, size_t bufferSize, uint32 maxCount)
{
	if (buffer == NULL || bufferSize == 0 || maxCount == 0)
		return B_BAD_VALUE;

	pthread_mutex_lock(&sAttrDirMutex);

	auto it = sAttrDirs.find(fd);
	if (it == sAttrDirs.end()) {
		pthread_mutex_unlock(&sAttrDirMutex);
		return B_FILE_ERROR;
	}

	attr_dir* dir = it->second;

	if (dir->currentIndex >= dir->attrNames.size()) {
		pthread_mutex_unlock(&sAttrDirMutex);
		return 0;  // No more entries
	}

	// Fill in dirent structure
	const std::string& attrName = dir->attrNames[dir->currentIndex];
	dir->currentDirent.d_ino = dir->currentIndex + 1;
	dir->currentDirent.d_off = 0;
	dir->currentDirent.d_reclen = sizeof(struct dirent);
	dir->currentDirent.d_type = DT_REG;
	strncpy(dir->currentDirent.d_name, attrName.c_str(), sizeof(dir->currentDirent.d_name) - 1);
	dir->currentDirent.d_name[sizeof(dir->currentDirent.d_name) - 1] = '\0';

	memcpy(buffer, &dir->currentDirent, sizeof(struct dirent));
	dir->currentIndex++;

	pthread_mutex_unlock(&sAttrDirMutex);
	return 1;  // One entry read
}


status_t
_rewind_attr_dir(int fd)
{
	pthread_mutex_lock(&sAttrDirMutex);

	auto it = sAttrDirs.find(fd);
	if (it == sAttrDirs.end()) {
		pthread_mutex_unlock(&sAttrDirMutex);
		return B_FILE_ERROR;
	}

	it->second->currentIndex = 0;
	pthread_mutex_unlock(&sAttrDirMutex);
	return B_OK;
}


status_t
_close_attr_dir(int fd)
{
	pthread_mutex_lock(&sAttrDirMutex);

	auto it = sAttrDirs.find(fd);
	if (it == sAttrDirs.end()) {
		pthread_mutex_unlock(&sAttrDirMutex);
		return B_FILE_ERROR;
	}

	attr_dir* dir = it->second;
	close(dir->fd);
	delete dir;
	sAttrDirs.erase(it);

	pthread_mutex_unlock(&sAttrDirMutex);
	return B_OK;
}


extern "C" int
fs_fopen_attr(int fd, const char* attribute, uint32 type, int openMode)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" ssize_t
fs_read_attr(int fd, const char* attribute, uint32 type,
		off_t pos, void* buffer, size_t readBytes) {
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	if (attribute == NULL || buffer == NULL)
		return B_BAD_VALUE;

	char name[XATTR_NAME_MAX + 1];
	if (snprintf(name, sizeof(name), "user.beos.%s", attribute) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;

	// Query the full size first
	ssize_t attrSize = fgetxattr(fd, name, NULL, 0);
	if (attrSize < 0)
		return B_ENTRY_NOT_FOUND;
	if (attrSize < (ssize_t)sizeof(uint32))
		return B_ENTRY_NOT_FOUND;

	// Format: [type:uint32][data bytes]
	ssize_t dataSize = attrSize - sizeof(uint32);
	if (pos < 0 || pos > dataSize)
		return B_BAD_VALUE;
	ssize_t bytesToCopy = dataSize - pos;
	if (bytesToCopy > (ssize_t)readBytes)
		bytesToCopy = (ssize_t)readBytes;
	if (bytesToCopy == 0)
		return 0;

	char* tempBuf = (char*)malloc(attrSize);
	if (!tempBuf)
		return B_NO_MEMORY;

	ssize_t got = fgetxattr(fd, name, tempBuf, attrSize);
	if (got != attrSize) {
		free(tempBuf);
		return B_ERROR;
	}

	// Since the bebook says type is just a hint, we simply ignore it.
	memcpy(buffer, tempBuf + sizeof(uint32) + pos, bytesToCopy);
	free(tempBuf);
	return bytesToCopy;
}


ssize_t
fs_write_attr(int fd, const char* attribute, uint32 type,
		off_t _pos, const void* buffer, size_t writeBytes) {
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	if (attribute == NULL || (buffer == NULL && writeBytes > 0))
		return B_BAD_VALUE;

	char name[XATTR_NAME_MAX + 1];
	if (snprintf(name, sizeof(name), "user.beos.%s", attribute) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;

	// Format: [type:uint32][data bytes]
	// _pos != 0 is not yet supported (read-modify-write); callers typically
	// write the full attribute in one call.
	size_t totalBytes = sizeof(type) + writeBytes;
	char* combinedBuffer = (char*)malloc(totalBytes);
	if (!combinedBuffer)
		return B_NO_MEMORY;

	memcpy(combinedBuffer, &type, sizeof(type));
	if (writeBytes > 0)
		memcpy(combinedBuffer + sizeof(type), buffer, writeBytes);

	ssize_t result = fsetxattr(fd, name, combinedBuffer, totalBytes, 0);

	free(combinedBuffer);

	if (result < 0)
		return B_ERROR;

	return (ssize_t)writeBytes;
}

status_t
fs_stat_attr(int fd, const char* attribute, struct attr_info* attrInfo) {
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	if (attribute == NULL)
		return B_BAD_VALUE;

	char name[XATTR_NAME_MAX + 1];
	if (snprintf(name, sizeof(name), "user.beos.%s", attribute) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;

	// Format: [type:uint32][data bytes]
	ssize_t attrSize = fgetxattr(fd, name, NULL, 0);
	if (attrSize < 0)
		return B_ENTRY_NOT_FOUND;
	if (attrSize < (ssize_t)sizeof(uint32))
		return B_ENTRY_NOT_FOUND;

	if (attrInfo) {
		// Must read the full xattr into a buffer first: fgetxattr with a
		// buffer smaller than the xattr returns -1/ERANGE, not a partial read.
		char* buf = (char*)malloc(attrSize);
		if (!buf)
			return B_NO_MEMORY;
		ssize_t got = fgetxattr(fd, name, buf, attrSize);
		if (got != attrSize) {
			free(buf);
			return B_ERROR;
		}
		memcpy(&attrInfo->type, buf, sizeof(uint32));
		attrInfo->size = (size_t)(attrSize - sizeof(uint32));
		free(buf);
	}

	return B_OK;
}


int
fs_open_attr(const char *path, const char *attribute,
	uint32 type, int openMode)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" int
fs_close_attr(int fd)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" int
_kern_open_attr(int fd, const char* path, const char* name,
		uint32 type, int openMode) {
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
fs_remove_attr(int fd, const char* name) {
	return _kern_remove_attr(fd, name);
}


status_t
_kern_remove_attr(int fd, const char* attribute) {
	CALLED();
	char name[XATTR_NAME_MAX + 1];
	if (snprintf(name, sizeof(name), "user.beos.%s", attribute) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;
	int ret = fremovexattr(fd, name);
	if (ret < 0)
		return B_ENTRY_NOT_FOUND;

	return B_OK;
}


status_t
fs_rename_attr(int fromFile, const char* fromName,
		int toFile, const char* toName) {
	return _kern_rename_attr(fromFile, fromName, toFile, toName);
}


extern "C" status_t
_kern_rename_attr(int fromFile, const char* fromName,
		int toFile, const char* toName) {
	CALLED();

	if (fromFile < 0 || toFile < 0)
		return B_FILE_ERROR;

	if (fromName == NULL || toName == NULL)
		return B_BAD_VALUE;

	char fromAttrName[XATTR_NAME_MAX + 1];
	char toAttrName[XATTR_NAME_MAX + 1];

	if (snprintf(fromAttrName, sizeof(fromAttrName), "user.beos.%s", fromName)
			>= (int)sizeof(fromAttrName))
		return B_NAME_TOO_LONG;
	if (snprintf(toAttrName, sizeof(toAttrName), "user.beos.%s", toName)
			>= (int)sizeof(toAttrName))
		return B_NAME_TOO_LONG;

	ssize_t size = fgetxattr(fromFile, fromAttrName, NULL, 0);
	if (size < 0)
		return B_ENTRY_NOT_FOUND;

	void* buffer = malloc(size);
	if (!buffer)
		return B_ERROR;

	ssize_t bytesRead = fgetxattr(fromFile, fromAttrName, buffer, size);
	if (bytesRead < 0) {
		free(buffer);
		return B_ERROR;
	}

	ssize_t result = fsetxattr(toFile, toAttrName, buffer, bytesRead, 0);
	free(buffer);

	if (result < 0)
		return B_ERROR;

	int ret = fremovexattr(fromFile, fromAttrName);
	if (ret < 0)
		return B_ERROR;

	return B_OK;
}
