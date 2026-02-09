/*
 *  Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <sys/ioctl.h>

#include "Team.h"
#include "../kernel/nexus/nexus/nexus.h"


sem_id
create_sem(int32 count, const char* name)
{
	if (count < 0)
		return B_BAD_VALUE;

	struct nexus_sem_exchange ex = {
		.count = count,
		.flags = 0,
		.timeout = 0,
		.name = name
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	return nexus_io(nexus, NEXUS_SEM_CREATE, &ex);
}


status_t
delete_sem(sem_id id)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	struct nexus_sem_exchange ex = { .id = id };

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	return nexus_io(nexus, NEXUS_SEM_DELETE, &ex);
}


status_t
acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (count < 1)
		return B_BAD_VALUE;

	struct nexus_sem_exchange ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = timeout
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	return nexus_io(nexus, NEXUS_SEM_ACQUIRE, &ex);
}


status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (count < 1)
		return B_BAD_VALUE;

	struct nexus_sem_exchange ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = 0
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	return nexus_io(nexus, NEXUS_SEM_RELEASE, &ex);
}


status_t
get_sem_count(sem_id id, int32* threadCount)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (threadCount == NULL)
		return B_BAD_VALUE;

	struct nexus_sem_exchange ex = { .id = id };

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	status_t ret = nexus_io(nexus, NEXUS_SEM_COUNT, &ex);
	if (ret == B_OK)
		*threadCount = ex.count;

	return ret;
}


status_t
_get_sem_info(sem_id id, struct sem_info* info, size_t infoSize)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (info == NULL || infoSize != sizeof(sem_info))
		return B_BAD_VALUE;

	struct nexus_sem_info newInfo = { .sem = id };
	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	status_t ret = nexus_io(nexus, NEXUS_SEM_INFO, &newInfo);
	if (ret == B_OK) {
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
