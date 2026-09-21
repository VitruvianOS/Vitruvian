/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "ResizeJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
ResizeJob::ResizeJob(PartitionReference* partition, PartitionReference* child,
		off_t size, off_t contentSize)
	:
	DiskDeviceJob(partition, child),
	fSize(size),
	fContentSize(contentSize)
{
}


// destructor
ResizeJob::~ResizeJob()
{
}


// Do
status_t
ResizeJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	int32 childChangeCounter = fChild->ChangeCounter();
	status_t error = _kern_resize_partition(fPartition->PartitionID(),
		&changeCounter, fChild->PartitionID(), &childChangeCounter, fSize,
		fContentSize);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	fChild->SetChangeCounter(childChangeCounter);

	return B_OK;
}


// AddToPlan
// no filesystem field; the helper probes the devnode with blkid
status_t
ResizeJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
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

	plan.AddResize(opID.String(), targetRef.String(), "",
		fSize / (1024 * 1024));
	return B_OK;
}
