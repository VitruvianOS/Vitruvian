/*
 * Copyright 2002-2011, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <fs_attr.h>

#include <syscall_utils.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <dirent_private.h>
#include <errno_private.h>
#include <syscalls.h>
#include <syscall_utils.h>


int
_kern_open_attr_dir(int file, const char *path, bool traverse)
{
	return 0;
}


//	#pragma mark -


extern "C" ssize_t
_kern_read_attr(int fd, const char* attribute, off_t pos,
	void* buffer, size_t readBytes)
{
#if defined(COSMOE_ATTRIBUTES)
	return fgetxattr(fd, attribute, buffer, readBytes);
#else
	printf( "Cosmoe: UNSUPPORTED: fs_read_attr\n" );
	return (ssize_t)-1;
#endif
}


extern "C" ssize_t
_kern_write_attr(int fd, const char* attribute, uint32 type, off_t pos,
	const void* buffer, size_t writeBytes)
{
#if defined(COSMOE_ATTRIBUTES)
	return fsetxattr(fd, attribute, buffer, readBytes, 0);
#else
	printf( "Cosmoe: UNSUPPORTED: fsetxattr\n" );
	return (ssize_t)-1;
#endif
}


extern "C" int
_kern_remove_attr(int fd, const char* attribute)
{
#if defined(COSMOE_ATTRIBUTES)
	return fremovexattr(fd, attribute);
#else
	printf( "Cosmoe: UNSUPPORTED: fs_remove_attr\n" );
	return -1;
#endif
}


extern "C" int
_kern_stat_attr(int fd, const char* attribute, struct attr_info* attrInfo)
{
	printf( "Cosmoe: UNIMPLEMENTED: fs_stat_attr\n" );
	return -1;
}


int
_kern_open_attr(int fd, const char *path, const char *attribute, uint32 type, int openMode)
{

}


extern "C" int
_kern_fopen_attr(int fd, const char* attribute, uint32 type, int openMode)
{

}


/*extern "C" int
fs_close_attr(int fd)
{
	status_t status = _kern_close(fd);

	RETURN_AND_SET_ERRNO(status);
}


extern "C" DIR*
fs_open_attr_dir(const char* path)
{
	return open_attr_dir(-1, path, true);
}


extern "C" DIR*
fs_lopen_attr_dir(const char* path)
{
	return open_attr_dir(-1, path, false);
}

extern "C" DIR*
fs_fopen_attr_dir(int fd)
{
	return open_attr_dir(fd, NULL, false);
}


extern "C" int
fs_close_attr_dir(DIR* dir)
{
	return closedir(dir);
}


extern "C" struct dirent*
fs_read_attr_dir(DIR* dir)
{
	return readdir(dir);
}


extern "C" void
fs_rewind_attr_dir(DIR* dir)
{
	rewinddir(dir);
}*/

