/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYSTEM_PORT_FDS_H
#define _SYSTEM_PORT_FDS_H


#include <OS.h>


// Port messages that carry file descriptors. The reader receives new
// descriptors (close-on-exec) for the same open files.
#define PORT_MAX_FDS	8


#ifdef __cplusplus
extern "C" {
#endif

// Returns B_OK, or an error; on B_BAD_VALUE no fd is attached.
extern status_t	write_port_fds(port_id port, int32 code, const void* buffer,
					size_t size, const int* fds, int32 fdCount, uint32 flags,
					bigtime_t timeout);

// On entry *fdCount is the capacity of fds; on return the number received.
// B_BUFFER_OVERFLOW leaves the message queued and sets *fdCount to the
// number it carries. Returns the message size, or an error.
extern ssize_t	read_port_fds(port_id port, int32* code, void* buffer,
					size_t size, int* fds, int32* fdCount, uint32 flags,
					bigtime_t timeout);

#ifdef __cplusplus
}
#endif


#endif	// _SYSTEM_PORT_FDS_H
