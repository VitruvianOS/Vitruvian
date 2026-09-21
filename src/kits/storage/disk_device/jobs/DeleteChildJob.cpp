/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "DeleteChildJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
DeleteChildJob::DeleteChildJob(PartitionReference* partition,
		PartitionReference* child)
	: DiskDeviceJob(partition, child)
{
}


// destructor
DeleteChildJob::~DeleteChildJob()
{
}


// Do
status_t
DeleteChildJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_delete_child_partition(fPartition->PartitionID(),
		&changeCounter, fChild->PartitionID(), fChild->ChangeCounter());
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetTo(-1, 0);

	return B_OK;
}


// AddToPlan
status_t
DeleteChildJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& /*createdIds*/)
{
	// deletes never target same-commit creates; no id mapping needed
	char devPath[B_PATH_NAME_LENGTH];
	status_t error = _kern_get_partition_path(fChild->PartitionID(), devPath,
		sizeof(devPath));
	if (error != B_OK)
		return error;

	plan.AddDelete(opID.String(), devPath);
	return B_OK;
}
