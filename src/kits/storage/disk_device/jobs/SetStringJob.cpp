/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
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


// SetFileSystem
// only the content-parameter op consults it; the rest probe in the helper
void
SetStringJob::SetFileSystem(const char* filesystem)
{
	fFileSystem.SetTo(filesystem != NULL ? filesystem : "");
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
// content label, GPT name, partition type, partition parameters and
// content parameters map; the remaining job types stay unsupported
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
		case B_DISK_DEVICE_JOB_SET_TYPE:
		case B_DISK_DEVICE_JOB_SET_PARAMETERS:
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

			if (fJobType == B_DISK_DEVICE_JOB_SET_NAME) {
				plan.AddSetGptName(opID.String(), targetRef.String(),
					fString);
			} else if (fJobType == B_DISK_DEVICE_JOB_SET_TYPE) {
				plan.AddSetType(opID.String(), targetRef.String(), fString);
			} else {
				plan.AddSetParameters(opID.String(), targetRef.String(),
					fString);
			}
			return B_OK;
		}

		case B_DISK_DEVICE_JOB_SET_CONTENT_PARAMETERS:
		{
			// only ext4 has a tune tool here; anything else is refused
			// for the caller instead of failing the whole plan
			if (fFileSystem != "ext4" || fString == NULL
				|| fString[0] == '\0') {
				return B_NOT_SUPPORTED;
			}

			BString rest(fString);
			while (!rest.IsEmpty()) {
				BString item;
				int32 space = rest.FindFirst(' ');
				if (space < 0) {
					item = rest;
					rest.Truncate(0);
				} else {
					rest.CopyInto(item, 0, space);
					rest.Remove(0, space + 1);
				}

				if (item.IsEmpty())
					continue;

				int32 eq = item.FindFirst('=');
				if (eq <= 0)
					return B_NOT_SUPPORTED;

				BString key;
				item.CopyInto(key, 0, eq);
				if (key != "reserved_percent" && key != "max_mount_count"
					&& key != "check_interval") {
					return B_NOT_SUPPORTED;
				}
			}

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

			plan.AddSetContentParameters(opID.String(), targetRef.String(),
				fFileSystem.String(), fString);
			return B_OK;
		}

		default:
			return B_NOT_SUPPORTED;
	}
}
