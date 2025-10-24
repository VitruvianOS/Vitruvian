/*
 *  Copyright 2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {

static dev_t sNexusVrefDev = -1;
static int sNexus = Team::GetVRefDescriptor(&sNexusVrefDev);

};


vref_id
create_vref(int fd)
{
	vref_id id = nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_CREATE, &fd);
	printf("create vref %d fd %d\n", id, fd);
	return id;
}


status_t
acquire_vref(vref_id id, int* fd)
{
	int ret = nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_ACQUIRE, &id);

	printf("acquire vref %d fd %d\n", id, ret);

	if (ret < 0)
		return ret;

	if (*fd != NULL)
		*fd = ret;

	return B_OK;
}


int
acquire_vref(vref_id id)
{
	int fd = nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_ACQUIRE, &id);
	printf("acquire vref %d fd %d\n", id, fd);
	return fd;
}


vref_id
clone_vref(vref_id id, int* fd)
{
	//int ret = nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_CLONE, &id);
	//if (ret < 0)
	//	return ret;

	//if (fd != NULL)
	//	*fd = ret;

	return id;
}


status_t
release_vref(vref_id id)
{
	printf("release vref %d\n", id);
	return nexus_io(BKernelPrivate::sNexus, NEXUS_VREF_RELEASE, &id);
}


dev_t
get_vref_dev()
{
	return BKernelPrivate::sNexusVrefDev;
}
