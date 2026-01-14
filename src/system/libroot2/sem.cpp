/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <sys/ioctl.h>

#include "Team.h"
#include "../kernel/nexus/nexus/nexus.h"


namespace BKernelPrivate {

static int sNexus = BKernelPrivate::Team::GetSemDescriptor();

};

using BKernelPrivate::sNexus;


sem_id
create_sem(int32 count, const char* name)
{
	struct nexus_sem_exchange ex = {
		.count = count,
		.flags = 0,
		.timeout = 0,
		.name = name
	};

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_SEM_CREATE, &ex);
}


status_t
delete_sem(sem_id id)
{
	struct nexus_sem_exchange ex = { .id = id };

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_SEM_DELETE, &ex);
}


status_t
acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	struct nexus_sem_exchange ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = timeout
	};

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_SEM_ACQUIRE, &ex);
}


status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	struct nexus_sem_exchange ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = 0
	};

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	return nexus_io(sNexus, NEXUS_SEM_RELEASE, &ex);
}


status_t
get_sem_count(sem_id id, int32* threadCount)
{
	struct nexus_sem_exchange ex = { .id = id };

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	status_t ret = nexus_io(sNexus, NEXUS_SEM_COUNT, &ex);
	if (ret == B_OK && threadCount)
		*threadCount = ex.count;

	return ret;
}


status_t
_get_sem_info(sem_id id, struct sem_info* info, size_t infoSize)
{
	struct nexus_sem_info newInfo { .sem = id };

	if (sNexus < 0) {
		sNexus = BKernelPrivate::Team::GetSemDescriptor();
		if (sNexus < 0)
			return B_ERROR;
	}

	status_t ret = nexus_io(sNexus, NEXUS_SEM_INFO, &newInfo);
	if (ret == B_OK && info != NULL) {
		info->sem = newInfo.sem;
		info->team = newInfo.team;
		strncpy(info->name, newInfo.name, B_OS_NAME_LENGTH);
		info->name[B_OS_NAME_LENGTH - 1] = '\0';
		info->count = newInfo.count;
		info->latest_holder = newInfo.latest_holder;
	}

	return ret;
}


status_t
_get_next_sem_info(team_id id, int32* cookie, struct sem_info* info, size_t size)
{
	UNIMPLEMENTED();
	// TODO proc
	return B_BAD_VALUE;
}


status_t
set_sem_owner(sem_id id, team_id team)
{
	UNIMPLEMENTED();
	// This is not really used beyond device drivers, deprecated.
	return B_UNSUPPORTED;
}
