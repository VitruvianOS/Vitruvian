#ifndef BEOS_BUILD_COMPATIBILITY_H
#define BEOS_BUILD_COMPATIBILITY_H


#define _IMPEXP_BE

#define __HAIKU_BEOS_COMPATIBLE_TYPES 1
#define ATOMIC_FUNCS_ARE_SYSCALLS 1
#define __x86_64__ 1

#define UNIMPLEMENTED()		printf("UNIMPLEMENTED %s\n",__PRETTY_FUNCTION__)

#define ULONGLONG_MAX   (0xffffffffffffffffULL)
#define LONGLONG_MAX	(9223372036854775807LL)
#define LONGLONG_MIN    (-9223372036854775807LL - 1)  /* these are Be specific */

#ifndef _ALIGNBYTES
#	define _ALIGNBYTES 7
#endif
#ifndef _ALIGN
#	define _ALIGN(p) (((unsigned)(p) + _ALIGNBYTES) & ~_ALIGNBYTES)
#endif

#include <config/HaikuConfig.h>
#include <config/types.h>
#include <SupportDefs.h>

#define DEBUG_SERVER
#define SERVER_TRUE 0

#if defined(HAIKU_HOST_PLATFORM_CYGWIN)
#	ifndef __addr_t_defined
#		define __addr_t_defined
#	endif
#endif

// DEFFILEMODE is not available on Cygwin, SunOS and when building with musl c
#if defined(HAIKU_HOST_PLATFORM_CYGWIN) || defined(HAIKU_HOST_PLATFORM_SUNOS) \
	|| !defined(DEFFILEMODE)
#ifndef DEFFILEMODE
#define DEFFILEMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)
#endif

// There's no ALLPERMS when building with musl c
#ifndef ALLPERMS
#	define ALLPERMS (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)
#endif

#ifndef S_IUMSK
#define	S_IUMSK 07777
#endif

#include <ctype.h>
#endif

#ifdef HAIKU_HOST_PLATFORM_SUNOS
#	include <limits.h>
#	ifndef NAME_MAX
#		define NAME_MAX	MAXNAMELEN
#	endif
#endif

typedef unsigned long	haiku_build_addr_t;
#define addr_t			haiku_build_addr_t

#include <Errors.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Is kernel-only under Linux.
#ifndef strlcpy
extern size_t   strlcpy(char* dest, const char* source, size_t length);
#endif
#ifndef strlcat
extern size_t	strlcat(char* dest, const char* source, size_t length);
#endif

// BeOS only
extern ssize_t  read_pos(int fd, off_t pos, void* buffer, size_t count);
extern ssize_t  write_pos(int fd, off_t pos, const void* buffer, size_t count);
extern ssize_t	readv_pos(int fd, off_t pos, const struct iovec* vec,
					size_t count);
extern ssize_t	writev_pos(int fd, off_t pos, const struct iovec* vec,
					size_t count);


// There's no O_NOTRAVERSE under Linux and FreeBSD -- we replace it with a flag
// that won't be used by our tools, preferrably a non-portable one; a fixed
// constant could always lead to trouble on the host.
// We can abuse this flag for our purposes as we filter it in libroot.
#ifndef O_NOTRAVERSE
#	ifdef O_NOCTTY
#		define O_NOTRAVERSE	O_NOCTTY
#	elif defined(O_RANDOM)
#		define O_NOTRAVERSE O_RANDOM
#	else
#		error "Search for a proper replacement value for O_NOTRAVERSE"
#	endif
#endif

#ifndef S_IUMSK
#	define S_IUMSK ALLPERMS
#endif

#include <string.h>
extern char* _haiku_build_strerror(int errnum);

// remap file descriptor functions
int		_haiku_build_fchmod(int fd, mode_t mode);
int		_haiku_build_fchmodat(int fd, const char* path, mode_t mode, int flag);
int		_haiku_build_fstat(int fd, struct stat* st);
int		_haiku_build_fstatat(int fd, const char* path, struct stat* st,
			int flag);
int		_haiku_build_mkdirat(int fd, const char* path, mode_t mode);
int		_haiku_build_mkfifoat(int fd, const char* path, mode_t mode);
int		_haiku_build_utimensat(int fd, const char* path,
			const struct timespec times[2], int flag);
int		_haiku_build_futimens(int fd, const struct timespec times[2]);
int		_haiku_build_faccessat(int fd, const char* path, int accessMode,
			int flag);
int		_haiku_build_fchdir(int fd);
int		_haiku_build_close(int fd);
int		_haiku_build_dup(int fd);
int		_haiku_build_dup2(int fd1, int fd2);
int		_haiku_build_linkat(int toFD, const char* toPath, int pathFD,
			const char* path, int flag);
int		_haiku_build_unlinkat(int fd, const char* path, int flag);
ssize_t	_haiku_build_readlinkat(int fd, const char* path, char* buffer,
			size_t bufferSize);
int		_haiku_build_symlinkat(const char* toPath, int fd,
			const char* symlinkPath);
int		_haiku_build_ftruncate(int fd, off_t newSize);
int		_haiku_build_fchown(int fd, uid_t owner, gid_t group);
int		_haiku_build_fchownat(int fd, const char* path, uid_t owner,
			gid_t group, int flag);
int		_haiku_build_mknodat(int fd, const char* name, mode_t mode, dev_t dev);
int		_haiku_build_creat(const char* path, mode_t mode);
int		_haiku_build_open(const char* path, int openMode, mode_t permissions);
int		_haiku_build_openat(int fd, const char* path, int openMode,
			mode_t permissions);
int		_haiku_build_fcntl(int fd, int op, int argument);
int		_haiku_build_renameat(int fromFD, const char* from, int toFD,
			const char* to);

#ifndef _HAIKU_BUILD_DONT_REMAP_FD_FUNCTIONS

#	if defined(HAIKU_HOST_USE_XATTR) && defined(HAIKU_HOST_PLATFORM_HAIKU)
#		define fs_read_attr			_haiku_build_fs_read_attr
#		define fs_write_attr		_haiku_build_fs_write_attr
#		define fs_remove_attr		_haiku_build_fs_remove_attr
#		define fs_stat_attr			_haiku_build_fs_stat_attr
#		define fs_open_attr			_haiku_build_fs_open_attr
#		define fs_fopen_attr		_haiku_build_fs_fopen_attr
#		define fs_close_attr		_haiku_build_fs_close_attr
#		define fs_open_attr_dir		_haiku_build_fs_open_attr_dir
#		define fs_fopen_attr_dir	_haiku_build_fs_fopen_attr_dir
#		define fs_close_attr_dir	_haiku_build_fs_close_attr_dir
#		define fs_read_attr_dir		_haiku_build_fs_read_attr_dir
#		define fs_rewind_attr_dir	_haiku_build_fs_rewind_attr_dir
#	endif

#endif	// _HAIKU_BUILD_DONT_REMAP_FD_FUNCTIONS

#ifdef __cplusplus
} // extern "C"
#endif

#endif	// BEOS_BUILD_COMPATIBILITY_H

