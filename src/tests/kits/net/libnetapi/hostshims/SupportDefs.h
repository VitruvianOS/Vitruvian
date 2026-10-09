/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_SUPPORTDEFS_H
#define _HOSTSHIMS_SUPPORTDEFS_H

#include <stdint.h>
#include <sys/types.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef intptr_t ssize_t_shim_unused;
typedef float float32;
typedef double float64;
typedef uint32_t type_code;
typedef int64_t bigtime_t;
typedef int32_t thread_id;
typedef uint32_t port_id;
typedef uint32_t area_id;
typedef uint32_t image_id;
typedef uint32_t team_id;
typedef uint64_t sem_id_shim;
typedef int status_t;
typedef unsigned int uint;

#ifndef B_PRId32
#define B_PRId32 "d"
#endif
#ifndef B_PRId64
#define B_PRId64 "lld"
#endif
#ifndef B_PRIdOFF
#define B_PRIdOFF "lld"
#endif
#ifndef B_PRIu32
#define B_PRIu32 "u"
#endif
#ifndef B_PRIu64
#define B_PRIu64 "llu"
#endif
#ifndef B_PRIx32
#define B_PRIx32 "x"
#endif
#ifndef B_PRIx64
#define B_PRIx64 "llx"
#endif

enum {
	B_OK = 0,
	B_ERROR = -1,
	B_NO_MEMORY = -11,
	B_BAD_VALUE = -22,
	B_ENTRY_NOT_FOUND = -2,
	B_NAME_NOT_FOUND = -3,
	B_TIMEOUT = -110,
	B_INTERRUPTED = -4,
	B_BUSY = -16,
	B_NOT_SUPPORTED = -95
};

#define B_INFINITE_TIMEOUT ((bigtime_t)0x7fffffffffffffffLL)

#ifndef B_NORMAL_PRIORITY
#define B_NORMAL_PRIORITY 10
#endif
#ifndef B_DISPLAY_PRIORITY
#define B_DISPLAY_PRIORITY 15
#endif
#ifndef B_URGENT_DISPLAY_PRIORITY
#define B_URGENT_DISPLAY_PRIORITY 15
#endif
#ifndef B_REAL_TIME_DISPLAY_PRIORITY
#define B_REAL_TIME_DISPLAY_PRIORITY 20
#endif
#ifndef B_MAX_PRIORITY
#define B_MAX_PRIORITY 32
#endif

#ifndef B_OS_NAME_LENGTH
#define B_OS_NAME_LENGTH 64
#endif

#endif
