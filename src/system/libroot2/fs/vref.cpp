/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "KernelDebug.h"
#include "syscalls.h"
#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {

static dev_t sNexusVRefDev = -1;
static int sNexus = Team::GetVRefDescriptor(&sNexusVRefDev);

};

using BKernelPrivate::sNexus;
using BKernelPrivate::sNexusVRefDev;


vref_id
create_vref(int fd)
{
	CALLED();

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_VREF_CREATE, &fd);
}


int
open_vref(vref_id id)
{
	CALLED();

	if (id < 0)
		return B_BAD_VALUE;

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_VREF_OPEN, &id);
}

status_t
acquire_vref_etc(vref_id id, int* fd)
{
	if (id < 0)
		return B_BAD_VALUE;

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	int ret = nexus_io(sNexus, NEXUS_VREF_ACQUIRE_FD, &id);

	if (ret < 0)
		return ret;

	if (fd != NULL)
		*fd = ret;

	return B_OK;
}

status_t
acquire_vref(vref_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_VREF_ACQUIRE, &id);
}


status_t
release_vref(vref_id id)
{
	if (id < 0)
		return B_BAD_VALUE;

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_VREF_RELEASE, &id);
}


dev_t
get_vref_dev()
{
	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVRefDev);
		if (sNexus < 0)
			return B_ERROR;
	}

	return sNexusVRefDev;
}
