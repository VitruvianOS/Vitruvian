/*
 *  Copyright 2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "Team.h"

#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {

static dev_t sNexusVrefDev = -1;
static int sNexusFd = BKernelPrivate::Team::GetVRefDescriptor(&sNexusVrefDev);

};


vref_id
create_vref(int fd)
{
	int nexus = BKernelPrivate::Team::GetVRefDescriptor();
	vref_id id = ioctl(nexus, NEXUS_VREF_CREATE, &fd);
	printf("create vref %d fd %d\n", id, fd);
	return id;
}


status_t
acquire_vref(vref_id id, int* fd)
{
	int nexus = BKernelPrivate::Team::GetVRefDescriptor();
	int f = ioctl(nexus, NEXUS_VREF_ACQUIRE, &id);
	printf("acquire vref %d fd %d\n", id, fd);
	return f;
}


int
acquire_vref(vref_id id)
{
	int nexus = BKernelPrivate::Team::GetVRefDescriptor();
	int fd = ioctl(nexus, NEXUS_VREF_ACQUIRE, &id);
	printf("acquire vref %d fd %d\n", id, fd);
	return fd;
}


vref_id
clone_vref(vref_id id, int* fd)
{
	
}


status_t
release_vref(vref_id id)
{
	printf("release vref %d\n", id);
	int nexus = BKernelPrivate::Team::GetVRefDescriptor();
	return ioctl(nexus, NEXUS_VREF_RELEASE, &id);
}


dev_t
get_vref_dev()
{
	return BKernelPrivate::sNexusVrefDev;
}
