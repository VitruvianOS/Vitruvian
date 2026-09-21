/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "UninitializeJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
UninitializeJob::UninitializeJob(PartitionReference* partition,
		PartitionReference* parent)
	: DiskDeviceJob(parent, partition)
{
}


// destructor
UninitializeJob::~UninitializeJob()
{
}


// Do
status_t
UninitializeJob::Do()
{
	bool haveParent = fPartition != NULL;
	int32 changeCounter = fChild->ChangeCounter();
	int32 parentChangeCounter = haveParent ? fPartition->ChangeCounter() : 0;
	partition_id parentID = haveParent ? fPartition->PartitionID() : B_INVALID_DEV;

	status_t error = _kern_uninitialize_partition(fChild->PartitionID(),
		&changeCounter, parentID, &parentChangeCounter);

	if (error != B_OK)
		return error;

	fChild->SetChangeCounter(changeCounter);
	if (haveParent)
		fPartition->SetChangeCounter(parentChangeCounter);

	return B_OK;
}


// AddToPlan
// maps to the "erase" op; unlike delete it keeps the table entry
status_t
UninitializeJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	// same-plan creates have no devnode yet; reference the create op id
	BString targetRef;
	if (const BString* createID = createdIds.Get(fChild))
		targetRef = *createID;
	else {
		char devPath[B_PATH_NAME_LENGTH];
		status_t error = _kern_get_partition_path(fChild->PartitionID(), devPath,
			sizeof(devPath));
		if (error != B_OK)
			return error;
		targetRef = devPath;
	}

	plan.AddErase(opID.String(), targetRef.String());
	return B_OK;
}
