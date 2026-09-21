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


// AddToPlan
// no plan-format defrag op exists; override makes the omission explicit
status_t
DefragmentJob::AddToPlan(PartitionPlanBuilder& /*plan*/,
	const BString& /*opID*/, PartitionOpIdMap& /*createdIds*/)
{
	return B_NOT_SUPPORTED;
}
