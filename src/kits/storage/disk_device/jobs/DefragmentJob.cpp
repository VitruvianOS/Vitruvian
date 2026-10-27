/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "DefragmentJob.h"

#include <syscalls.h>

#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
DefragmentJob::DefragmentJob(PartitionReference* partition)
	: DiskDeviceJob(partition)
{
}


// destructor
DefragmentJob::~DefragmentJob()
{
}


// Do
status_t
DefragmentJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	status_t error = _kern_defragment_partition(fPartition->PartitionID(),
		&changeCounter);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}


// SetFileSystem
void
DefragmentJob::SetFileSystem(const char* filesystem)
{
	fFileSystem.SetTo(filesystem != NULL ? filesystem : "");
}


// AddToPlan
// only ext4 and btrfs have a defragment tool; the rest stay unsupported
status_t
DefragmentJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	if (fFileSystem != "ext4" && fFileSystem != "btrfs")
		return B_NOT_SUPPORTED;

	// same-plan creates have no devnode; reference the create op id
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

	plan.AddDefrag(opID.String(), targetRef.String(), fFileSystem.String());
	return B_OK;
}
