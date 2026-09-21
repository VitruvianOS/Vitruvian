/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "RepairJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
RepairJob::RepairJob(PartitionReference* partition, bool checkOnly)
	:
	DiskDeviceJob(partition),
	fCheckOnly(checkOnly)
{
}


// destructor
RepairJob::~RepairJob()
{
}


// Do
status_t
RepairJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_repair_partition(fPartition->PartitionID(),
		&changeCounter, fCheckOnly);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}


// AddToPlan
// no filesystem field; the helper probes it with blkid
status_t
RepairJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	// same-plan creates have no devnode yet; reference the create op id
	BString targetRef;
	if (const BString* createID = createdIds.Get(fPartition))
		targetRef = *createID;
	else {
		char devPath[B_PATH_NAME_LENGTH];
		status_t error = _kern_get_partition_path(fPartition->PartitionID(), devPath,
			sizeof(devPath));
		if (error != B_OK)
			return error;
		targetRef = devPath;
	}

	plan.AddRepair(opID.String(), targetRef.String(), "", fCheckOnly);
	return B_OK;
}
