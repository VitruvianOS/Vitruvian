/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>


status_t
_kern_stop_notifying(port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_start_watching(dev_t device, ino_t node, uint32 flags,
	port_id port, uint32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_stop_watching(dev_t device, ino_t node, port_id port,
	uint32 token)
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
_kstart_watching_vnode_(dev_t device, ino_t node,
	uint32 flags, port_id port, int32 handlerToken)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kstop_watching_vnode_(dev_t device, ino_t node,
	port_id port, int32 handlerToken)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
