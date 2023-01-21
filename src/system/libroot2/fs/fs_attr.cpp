/*
 * Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include <fs_attr.h>

#include <sys/xattr.h>

#include <syscalls.h>

#include "KernelDebug.h"


static DIR *
open_attr_dir(int fd, const char* path,
		bool traverseLeafLink) {
	UNIMPLEMENTED();
	return NULL;
}


extern "C" DIR*
fs_open_attr_dir(const char* path)
{
	return NULL;
}


extern "C" DIR*
fs_fopen_attr_dir(int fd)
{
	return NULL;
}


extern "C" struct dirent*
fs_read_attr_dir(DIR* dir)
{
	return NULL;
}


extern "C" int
fs_close_attr_dir(DIR* dir)
{
	return B_ERROR;
}


extern "C" int
fs_fopen_attr(int fd, const char* attribute, uint32 type, int openMode)
{
	return B_ERROR;
}


extern "C" ssize_t
fs_read_attr(int fd, const char* attribute, uint32 /*_type*/,
		off_t pos, void* buffer, size_t readBytes) {
	CALLED();

	return fgetxattr(fd, attribute, buffer, readBytes);
}


extern "C" ssize_t
fs_write_attr(int fd, const char* attribute, uint32 type,
		off_t pos, const void* buffer, size_t readBytes) {
	CALLED();

	return fsetxattr(fd, attribute, buffer, readBytes, 0);
}


status_t
fs_stat_attr(int fd, const char* attribute,
		struct attr_info* attrInfo) {
	printf("%s\n", attribute);
	UNIMPLEMENTED();
	return B_ERROR;
}


int
fs_open_attr(const char *path, const char *attribute,
	uint32 type, int openMode)
{
}


extern "C" int
fs_close_attr(int fd)
{
}


extern "C" int
_kern_open_attr(int fd, const char* path, const char* name,
		uint32 type, int openMode) {
	UNIMPLEMENTED();
	return B_ERROR;
}


extern int
_kern_open_attr_dir(int fd, const char *path, bool traverseLeafLink)
{
	UNIMPLEMENTED();
	return -1;
}


status_t
fs_remove_attr(int fd, const char* name) {
	CALLED();

	return fremovexattr(fd, name);
}


status_t
_kern_remove_attr(int fd, const char* name) {
	CALLED();

	return B_ERROR;
}


status_t
fs_rename_attr(int fromFile, const char* fromName,
		int toFile, const char* toName) {
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_rename_attr(int fromFile, const char* fromName,
		int toFile, const char* toName) {
	UNIMPLEMENTED();
	return B_ERROR;
}
