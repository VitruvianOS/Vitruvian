#ifndef LINUX_BUILD_COMPATIBILITY_H
#define LINUX_BUILD_COMPATIBILITY_H

#include <config/HaikuConfig.h>
#include <config/types.h>

#include <SupportDefs.h>

#include <string.h>
#include <time.h>
#include <sys/ioctl.h>


#define SYMLOOP_MAX _POSIX_SYMLOOP_MAX
#define O_RWMASK O_ACCMODE

#define __VOS__ 1

#define __HAIKU_PRIMARY_PACKAGING_ARCH "x86_64"

#define _IMPEXP_BE

#define UNIMPLEMENTED() printf("UNIMPLEMENTED %s\n",__PRETTY_FUNCTION__)

#define ULONGLONG_MAX   (0xffffffffffffffffULL)
#define LONGLONG_MAX	(9223372036854775807LL)
#define LONGLONG_MIN    (-9223372036854775807LL - 1)  /* these are Be specific */

#ifndef _ALIGNBYTES
#	define _ALIGNBYTES 7
#endif
#ifndef _ALIGN
#	define _ALIGN(p) (((unsigned)(p) + _ALIGNBYTES) & ~_ALIGNBYTES)
#endif

// There's no ALLPERMS when building with musl c
#ifndef ALLPERMS
#	define ALLPERMS (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)
#endif

#ifndef S_IUMSK
#define	S_IUMSK 07777
#endif

#include <ctype.h>

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

#define recursive_lock_init(lock, name)    __recursive_lock_init(lock, name)
#define recursive_lock_init_etc(lock, name, flags) \
      __recursive_lock_init_etc(lock, name, flags)
#define recursive_lock_destroy(lock)       __recursive_lock_destroy(lock)
#define recursive_lock_lock(lock)          __recursive_lock_lock(lock)
#define recursive_lock_unlock(lock)        __recursive_lock_unlock(lock)
#define recursive_lock_get_recursion(lock) __recursive_lock_get_recursion(lock)

#ifdef __cplusplus
} // extern "C"
#endif

#endif	// LINUX_BUILD_COMPATIBILITY_H

