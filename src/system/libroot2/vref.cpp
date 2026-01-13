/*
 *  Copyright 2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "KernelDebug.h"
#include "syscalls.h"
#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {

static dev_t sNexusVrefDev = -1;
static int sNexus = Team::GetVRefDescriptor(&sNexusVrefDev);

};


vref_id
create_vref(int fd)
{
	CALLED();

	return nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_CREATE, &fd);
}


int
open_vref(vref_id id)
{
	CALLED();

	return nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_OPEN, &id);
}

status_t
acquire_vref_etc(vref_id id, int* fd)
{
	int ret = nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_ACQUIRE_FD, &id);

	if (ret != B_OK)
		return ret;

	if (fd != NULL)
		*fd = ret;

	return B_OK;
}

status_t
acquire_vref(vref_id id)
{
	return nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_ACQUIRE, &id);
}


status_t
release_vref(vref_id id)
{
	return nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_RELEASE, &id);
}


dev_t
get_vref_dev()
{
	return BKernelPrivate::sNexusVrefDev;
}
