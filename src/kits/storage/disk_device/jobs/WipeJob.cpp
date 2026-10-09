/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WipeJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
WipeJob::WipeJob(PartitionReference* partition, bool full)
	:
	DiskDeviceJob(partition),
	fFull(full)
{
}


// destructor
WipeJob::~WipeJob()
{
}


// Do
// no in-kernel wipe op; the privileged helper path is the real one
status_t
WipeJob::Do()
{
	return B_NOT_SUPPORTED;
}


// AddToPlan
status_t
WipeJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	// same-plan creates have no devnode yet; reference the create op id
	BString targetRef;
	if (const BString* createID = createdIds.Get(fPartition))
		targetRef = *createID;
	else {
		char devPath[B_PATH_NAME_LENGTH];
		status_t error = _kern_get_partition_path(fPartition->PartitionID(),
			devPath, sizeof(devPath));
		if (error != B_OK)
			return error;
		targetRef = devPath;
	}

	plan.AddWipe(opID.String(), targetRef.String(), fFull);
	return B_OK;
}
