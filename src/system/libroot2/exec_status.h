/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_EXEC_STATUS
#define _LIBROOT2_EXEC_STATUS

#include <SupportDefs.h>


namespace BKernelPrivate {


// Runs the dynamic loader in list mode on path. Returns B_MISSING_LIBRARY
// when the target's dependencies cannot be resolved; B_OK for anything the
// check does not cover (scripts, static binaries, unreadable files), so
// execve() stays the authority for those.
status_t	CheckDynamicLoader(const char* path, char* detail,
				size_t detailSize);

// Parent side of the load_image exec status pipe: blocks until the child
// execs (B_OK, the CLOEXEC write end closed) or it reports a failure status.
status_t	WaitForExecStatus(int fd, bigtime_t timeout);

// Closes both ends after a failed or abandoned load.
void		CloseExecStatusPipe(int readFd, int writeFd);


}


#endif // _LIBROOT2_EXEC_STATUS
