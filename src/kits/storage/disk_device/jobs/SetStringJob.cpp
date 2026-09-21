/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "SetStringJob.h"

#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
SetStringJob::SetStringJob(PartitionReference* partition,
		PartitionReference* child)
	:
	DiskDeviceJob(partition, child),
	fString(NULL)
{
}


// destructor
SetStringJob::~SetStringJob()
{
	free(fString);
}


// Init
status_t
SetStringJob::Init(const char* string, uint32 jobType)
{
	switch (jobType) {
		case B_DISK_DEVICE_JOB_SET_NAME:
		case B_DISK_DEVICE_JOB_SET_CONTENT_NAME:
		case B_DISK_DEVICE_JOB_SET_TYPE:
		case B_DISK_DEVICE_JOB_SET_PARAMETERS:
		case B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS:
			break;
		default:
			return B_BAD_VALUE;
	}

	fJobType = jobType;
	SET_STRING_RETURN_ON_ERROR(fString, string);

	return B_OK;
}


// Do
status_t
SetStringJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();
	int32 childChangeCounter = (fChild ? fChild->ChangeCounter() : 0);
	status_t error;
	bool updateChildChangeCounter = false;

	switch (fJobType) {
		case B_DISK_DEVICE_JOB_SET_NAME:
			error = _kern_set_partition_name(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_CONTENT_NAME:
			error = _kern_set_partition_content_name(fPartition->PartitionID(),
				&changeCounter, fString);
			break;
		case B_DISK_DEVICE_JOB_SET_TYPE:
			error = _kern_set_partition_type(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_PARAMETERS:
			error = _kern_set_partition_parameters(fPartition->PartitionID(),
				&changeCounter, fChild->PartitionID(), &childChangeCounter,
				fString);
			updateChildChangeCounter = true;
			break;
		case B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS:
			error = _kern_set_partition_content_parameters(
				fPartition->PartitionID(), &changeCounter, fString);
			break;
		default:
			return B_BAD_VALUE;
	}

	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);
	if (updateChildChangeCounter)
		fChild->SetChangeCounter(childChangeCounter);

	return B_OK;
}


// AddToPlan
// only content label and GPT name map to the plan; the rest do not
status_t
SetStringJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	switch (fJobType) {
		case B_DISK_DEVICE_JOB_SET_CONTENT_NAME:
		{
			// same-plan creates lack a devnode; use create op id
			BString targetRef;
			if (const BString* createID = createdIds.Get(fPartition))
				targetRef = *createID;
			else {
				char devPath[B_PATH_NAME_LENGTH];
				status_t error = _kern_get_partition_path(
					fPartition->PartitionID(), devPath, sizeof(devPath));
				if (error != B_OK)
					return error;
				targetRef = devPath;
			}

			plan.AddSetLabel(opID.String(), targetRef.String(), "", fString);
			return B_OK;
		}

		case B_DISK_DEVICE_JOB_SET_NAME:
		{
			BString targetRef;
			if (const BString* createID = createdIds.Get(fChild))
				targetRef = *createID;
			else {
				char devPath[B_PATH_NAME_LENGTH];
				status_t error = _kern_get_partition_path(
					fChild->PartitionID(), devPath, sizeof(devPath));
				if (error != B_OK)
					return error;
				targetRef = devPath;
			}

			plan.AddSetGptName(opID.String(), targetRef.String(), fString);
			return B_OK;
		}

		default:
			return B_NOT_SUPPORTED;
	}
}
