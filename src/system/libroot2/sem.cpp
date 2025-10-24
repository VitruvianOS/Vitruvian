/*
 *  Copyright 2025, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include "Team.h"
#include "KernelDebug.h"

#include "../kernel/nexus/nexus/nexus.h"


//static int sNexus = BKernelPrivate::Team::GetSemDescriptor();


sem_id
create_sem_etc(int32 count, const char* name, team_id _owner)
{
	CALLED();

	struct nexus_sem_exchange exchange;
	exchange.count = count;
	exchange.name = name;

	int sNexus = BKernelPrivate::Team::GetSemDescriptor();
	int ret = nexus_io(sNexus, NEXUS_SEM_CREATE, &exchange);
	if (ret < 0)
		return ret;

	return exchange.id;
}


sem_id
create_sem(int32 count, const char* name)
{
	return create_sem_etc(count, name, getpid());
}


status_t
delete_sem(sem_id id)
{
	CALLED();

	struct nexus_sem_exchange exchange;
	exchange.id = id;

	int sNexus = BKernelPrivate::Team::GetSemDescriptor();
	return nexus_io(sNexus, NEXUS_SEM_DELETE, &exchange);
}


status_t
acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


status_t
acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
	CALLED();

	struct nexus_sem_exchange exchange;
	exchange.id = id;
	exchange.count = count;
	exchange.flags = flags;
	exchange.timeout = timeout;

	int sNexus = BKernelPrivate::Team::GetSemDescriptor();
	return nexus_io(sNexus, NEXUS_SEM_ACQUIRE, &exchange);
}


status_t
release_sem(sem_id id)
{
	CALLED();
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	CALLED();

	struct nexus_sem_exchange exchange;
	exchange.id = id;
	exchange.count = count;
	exchange.flags = flags;

	int sNexus = BKernelPrivate::Team::GetSemDescriptor();
	return nexus_io(sNexus, NEXUS_SEM_RELEASE, &exchange);
}


status_t
get_sem_count(sem_id id, int32* thread_count)
{
	CALLED();

	struct nexus_sem_exchange exchange;
	exchange.id = id;

	int sNexus = BKernelPrivate::Team::GetSemDescriptor();
	int ret = nexus_io(sNexus, NEXUS_SEM_COUNT, &exchange);
	if (ret < 0)
		return ret;

	*thread_count = exchange.count;

	return B_OK;
}


status_t
_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	CALLED();
	memset(info, 0, size);
	// TODO proc
	return B_OK;
}


status_t
_get_next_sem_info(team_id id, int32* cookie,
	struct sem_info* info, size_t size)
{
	CALLED();
	// TODO proc
	return B_BAD_VALUE;
}


status_t
set_sem_owner(sem_id id, team_id team)
{
	UNIMPLEMENTED();
	// This is not really used beyond device drivers
	// TODO: Deprecate someday
	return B_OK;
}
