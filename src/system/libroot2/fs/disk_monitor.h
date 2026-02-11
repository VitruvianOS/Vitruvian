/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT_DISK_MONITOR
#define _LIBROOT_DISK_MONITOR

#include <OS.h>


namespace BKernelPrivate {


status_t	start_mount_watching(port_id port, uint32 token);
status_t	stop_mount_watching(port_id port, uint32 token);
void		stop_all_watching_for_target(port_id port, uint32 token);


}

#endif // _LIBROOT_DISK_MONITOR
