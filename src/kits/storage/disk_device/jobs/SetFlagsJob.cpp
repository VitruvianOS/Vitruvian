/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */

#include "SetFlagsJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


SetFlagsJob::SetFlagsJob(PartitionReference* partition,
		PartitionReference* child)
	: DiskDeviceJob(partition, child)
{
}


SetFlagsJob::~SetFlagsJob()
{
}


status_t
SetFlagsJob::Init(const BMessage& flags)
{
	fFlags = flags;
	return B_OK;
}


status_t
SetFlagsJob::Do()
{
	// No _kern_set_partition_flags syscall; flags are applied via the plan only.
	return B_NOT_SUPPORTED;
}


status_t
SetFlagsJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& /*createdIds*/)
{
	char devPath[B_PATH_NAME_LENGTH];
	status_t error = _kern_get_partition_path(fChild->PartitionID(), devPath,
		sizeof(devPath));
	if (error != B_OK)
		return error;

	plan.AddSetFlags(opID.String(), devPath, fFlags);
	return B_OK;
}
