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

#ifndef O_NOTRAVERSE
#	ifdef __VOS__
#		define O_NOTRAVERSE AT_SYMLINK_NOFOLLOW
#	else
#		error "Search for a proper replacement value for O_NOTRAVERSE"
#	endif
#endif

#ifndef S_IUMSK
#	define S_IUMSK ALLPERMS
#endif

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

