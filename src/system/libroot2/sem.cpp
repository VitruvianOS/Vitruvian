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

	struct nexus_sem_create ex = {
		.name = name,
		.count = count,
		.id = 0,
		.ret = B_OK
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_CREATE, &ex) < 0)
		return B_ERROR;
	if (ex.ret != B_OK)
		return ex.ret;
	return ex.id;
}


status_t
delete_sem(sem_id id)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	struct nexus_sem_delete_req ex = { .id = id, .ret = B_OK };

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_DELETE, &ex) < 0)
		return B_ERROR;
	return ex.ret;
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

	struct nexus_sem_op ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = timeout,
		.ret = B_OK
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_ACQUIRE, &ex) < 0)
		return B_ERROR;
	return ex.ret;
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

	struct nexus_sem_op ex = {
		.id = id,
		.count = count,
		.flags = flags,
		.timeout = 0,
		.ret = B_OK
	};

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_RELEASE, &ex) < 0)
		return B_ERROR;
	return ex.ret;
}


status_t
get_sem_count(sem_id id, int32* threadCount)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (threadCount == NULL)
		return B_BAD_VALUE;

	struct nexus_sem_count_req ex = { .id = id, .count = 0, .ret = B_OK };

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_COUNT, &ex) < 0)
		return B_ERROR;
	if (ex.ret == B_OK)
		*threadCount = ex.count;
	return ex.ret;
}


status_t
_get_sem_info(sem_id id, struct sem_info* info, size_t infoSize)
{
	if (id < 0)
		return B_BAD_SEM_ID;

	if (info == NULL || infoSize != sizeof(sem_info))
		return B_BAD_VALUE;

	struct nexus_sem_info_req req;
	memset(&req, 0, sizeof(req));
	req.id = id;

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_INFO, &req) < 0)
		return B_ERROR;
	if (req.ret == B_OK) {
		info->sem = req.info.sem;
		info->team = req.info.team;
		strncpy(info->name, req.info.name, B_OS_NAME_LENGTH);
		info->name[B_OS_NAME_LENGTH - 1] = '\0';
		info->count = req.info.count;
		info->latest_holder = req.info.latest_holder;
	}
	return req.ret;
}


status_t
_get_next_sem_info(team_id id, int32* cookie, struct sem_info* info, size_t size)
{
	if (cookie == NULL || info == NULL || size != sizeof(sem_info))
		return B_BAD_VALUE;

	struct nexus_sem_next_info nextInfo;
	memset(&nextInfo, 0, sizeof(nextInfo));
	nextInfo.team = id;
	nextInfo.cookie = *cookie;

	int nexus = BKernelPrivate::Team::GetSemDescriptor();
	if (nexus < 0)
		return B_ERROR;

	if (nexus_io(nexus, NEXUS_SEM_NEXT_INFO, &nextInfo) < 0)
		return B_ERROR;
	if (nextInfo.ret == B_OK) {
		info->sem = nextInfo.info.sem;
		info->team = nextInfo.info.team;
		strncpy(info->name, nextInfo.info.name, B_OS_NAME_LENGTH);
		info->name[B_OS_NAME_LENGTH - 1] = '\0';
		info->count = nextInfo.info.count;
		info->latest_holder = nextInfo.info.latest_holder;
		*cookie = nextInfo.cookie;
	}
	return nextInfo.ret;
}


status_t
set_sem_owner(sem_id id, team_id team)
{
	UNIMPLEMENTED();
	// This is not really used beyond device drivers, deprecated.
	return B_UNSUPPORTED;
}
