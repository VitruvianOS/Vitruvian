/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <NodeMonitor.h>
#include <OS.h>

#include <pthread.h>

#include "disk_monitor.h"
#include "../kernel/nexus/nexus/nexus.h"
#include "../kernel/nexus/nexus/node_monitor.h"
#include "Team.h"


extern "C" {


status_t
_kern_start_watching(dev_t device, ino_t node, uint32 flags,
	port_id port, uint32 token)
{
	printf("_kern_start_watching\n");

#ifdef __ENABLE_NODE_MONITOR__
	if (flags & B_WATCH_MOUNT) {
		status_t err = BKernelPrivate::start_mount_watching(port, token);
		if (err != B_OK)
			return err;

		if ((flags & ~B_WATCH_MOUNT) == 0)
			return B_OK;
		
		flags &= ~B_WATCH_MOUNT;
	}

	int nodeMonitor = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nodeMonitor < 0)
		return B_ENTRY_NOT_FOUND;

	int nodeFD = -1;
	if (device == get_vref_dev())
		nodeFD = open_vref(node);

	struct nexus_watch_fd req = {
		.fd = nodeFD,
		.flags = flags,
		.port = port,
		.token = token
	};

	status_t ret = nexus_io(nodeMonitor, NEXUS_START_WATCHING, &req);
	close(nodeFD);
	return ret;
#else
	return B_OK;
#endif
}


status_t
_kern_stop_watching(dev_t device, ino_t node, port_id port, uint32 token)
{
#ifdef __ENABLE_NODE_MONITOR__
	BKernelPrivate::stop_mount_watching(port, token);

	int nodeMonitor = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nodeMonitor < 0)
		return B_ENTRY_NOT_FOUND;

	struct nexus_unwatch_fd req = {
		.device = (uint64_t)device,
		.node = (uint64_t)node,
		.port = port,
		.token = token
	};

	return nexus_io(nodeMonitor, NEXUS_STOP_WATCHING, &req);
#else
	return B_OK;
#endif
}


status_t
_kern_stop_notifying(port_id port, uint32 token)
{
#ifdef __ENABLE_NODE_MONITOR__
	BKernelPrivate::stop_all_watching_for_target(port, token);

	int nodeMonitor = BKernelPrivate::Team::GetNodeMonitorDescriptor();
	if (nodeMonitor < 0)
		return B_ENTRY_NOT_FOUND;

	struct nexus_stop_notifying req = { .port = port, .token = token };
	return nexus_io(nodeMonitor, NEXUS_STOP_NOTIFYING, &req);
#else
	return B_OK;
#endif
}


} // extern "C"
