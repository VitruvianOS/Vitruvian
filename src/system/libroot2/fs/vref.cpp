/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "KernelDebug.h"
#include "syscalls.h"
#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


vref_id
create_vref(int fd, vref_key* outKey)
{
	CALLED();

	if (outKey == NULL)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetVRefDescriptor(nullptr);
	if (nexus < 0)
		return B_ERROR;

	struct nexus_vref_create req = { fd, 0, 0 };
	long ret = nexus_io(nexus, NEXUS_VREF_CREATE, &req);
	if (ret < 0)
		return (vref_id)ret;

	*outKey = req.key;
	return req.id;
}


status_t
acquire_vref(vref_id id, vref_key* outKey)
{
	if (id < 0 || outKey == NULL)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetVRefDescriptor(nullptr);
	if (nexus < 0)
		return B_ERROR;

	struct nexus_vref_op req = { id, 0 };
	long ret = nexus_io(nexus, NEXUS_VREF_ACQUIRE, &req);
	if (ret < 0)
		return (status_t)ret;

	*outKey = req.key;
	return B_OK;
}


int
open_vref(vref_id id, vref_key key)
{
	CALLED();

	if (id < 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetVRefDescriptor(nullptr);
	if (nexus < 0)
		return B_ERROR;

	struct nexus_vref_open req = { id, key, 0, -1 };
	long ret = nexus_io(nexus, NEXUS_VREF_OPEN, &req);
	if (ret < 0)
		return (int)ret;
	return req.fd_out;
}


status_t
release_vref(vref_id id, vref_key key)
{
	if (id < 0)
		return B_BAD_VALUE;

	int nexus = BKernelPrivate::Team::GetVRefDescriptor(nullptr);
	if (nexus < 0)
		return B_ERROR;

	struct nexus_vref_op req = { id, key };
	long ret = nexus_io(nexus, NEXUS_VREF_RELEASE, &req);
	return (status_t)ret;
}


dev_t
get_vref_dev()
{
	static dev_t sCachedVRefDev = B_INVALID_DEV;
	if (sCachedVRefDev != B_INVALID_DEV)
		return sCachedVRefDev;

	dev_t vrefDev = -1;
	int nexus = BKernelPrivate::Team::GetVRefDescriptor(&vrefDev);
	if (nexus < 0)
		return B_INVALID_DEV;

	sCachedVRefDev = vrefDev;
	return sCachedVRefDev;
}
