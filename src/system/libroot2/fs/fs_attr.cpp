/*
 * Copyright 2019-2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <fs_attr.h>

#include <sys/xattr.h>

#include "KernelDebug.h"


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
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" int
fs_close_attr_dir(DIR* dir)
{
	UNIMPLEMENTED();
	return B_ERROR;
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

	if (attribute == NULL || buffer == NULL || readBytes < 0)
		return B_BAD_VALUE;

	char name[B_OS_NAME_LENGTH];
	snprintf(name, B_OS_NAME_LENGTH, "user.beos.%s", attribute);

	ssize_t totalBytesRead = fgetxattr(fd, name, buffer, readBytes+1);
	if (totalBytesRead < 1 || totalBytesRead != readBytes+1)
		return B_ENTRY_NOT_FOUND;

	// Since the bebook says type is just an hint and we can read any
	// attr with any type, we simply ignore it.

	return totalBytesRead-1;
}


ssize_t
fs_write_attr(int fd, const char* attribute, uint32 type,
		off_t _pos, const void* buffer, size_t writeBytes) {
	CALLED();

	// TODO should we check type for types in TypeConstants.h?

	if (fd < 0)
		return B_FILE_ERROR;

	if (attribute == NULL || buffer == NULL || writeBytes < 0)
		return B_BAD_VALUE;

	char name[B_OS_NAME_LENGTH];
	snprintf(name, B_OS_NAME_LENGTH, "user.beos.%s", attribute);

	size_t totalBytes = writeBytes + sizeof(type);
	char* combinedBuffer = (char*)malloc(totalBytes);
	if (!combinedBuffer)
		return B_NO_MEMORY;

	memcpy(combinedBuffer, (uint32*)buffer, writeBytes);
	combinedBuffer[writeBytes] = type;

	ssize_t result = fsetxattr(fd, name, combinedBuffer, totalBytes, 0);

	free(combinedBuffer);

	if (result < 0)
		return B_ERROR;

	return result;
}

status_t
fs_stat_attr(int fd, const char* attribute, struct attr_info* attrInfo) {
	CALLED();

	if (fd < 0)
		return B_FILE_ERROR;

	if (attribute == NULL)
		return B_BAD_VALUE;

	char name[B_OS_NAME_LENGTH];
	snprintf(name, B_OS_NAME_LENGTH, "user.beos.%s", attribute);

	uint32 buf[1];
	int size = fgetxattr(fd, name, buf, sizeof(buf));
	if (size < 0)
		return B_ENTRY_NOT_FOUND;

	if (attrInfo) {
		attrInfo->size = size - sizeof(uint32);
		attrInfo->type = buf[0];
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
	char name[B_OS_NAME_LENGTH];
	snprintf(name, B_OS_NAME_LENGTH, "user.beos.%s", attribute);
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

	char fromAttrName[B_OS_NAME_LENGTH];
	char toAttrName[B_OS_NAME_LENGTH];

	snprintf(fromAttrName, B_OS_NAME_LENGTH, "user.beos.%s", fromName);
	snprintf(toAttrName, B_OS_NAME_LENGTH, "user.beos.%s", toName);

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
