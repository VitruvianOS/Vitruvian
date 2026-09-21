/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "InitializeJob.h"

#include <strings.h>

#include <ddm_userland_interface_defs.h>
#include <syscalls.h>

#include "DiskDeviceUtils.h"
#include "PartitionPlanBuilder.h"
#include "PartitionReference.h"


// constructor
InitializeJob::InitializeJob(PartitionReference* partition)
	:
	DiskDeviceJob(partition),
	fDiskSystem(NULL),
	fName(NULL),
	fParameters(NULL),
	fIsDevice(false)
{
}


// destructor
InitializeJob::~InitializeJob()
{
	free(fDiskSystem);
	free(fName);
	free(fParameters);
}


// Init
status_t
InitializeJob::Init(const char* diskSystem, const char* name,
	const char* parameters)
{
	SET_STRING_RETURN_ON_ERROR(fDiskSystem, diskSystem);
	SET_STRING_RETURN_ON_ERROR(fName, name);
	SET_STRING_RETURN_ON_ERROR(fParameters, parameters);

	return B_OK;
}


// Do
status_t
InitializeJob::Do()
{
	int32 changeCounter = fPartition->ChangeCounter();

	status_t error = _kern_initialize_partition(fPartition->PartitionID(),
		&changeCounter, fDiskSystem, fName, fParameters);
	if (error != B_OK)
		return error;

	fPartition->SetChangeCounter(changeCounter);

	return B_OK;
}


// SetPlanMetadata
void
InitializeJob::SetPlanMetadata(bool isDevice, const char* role,
	const BMessage& options)
{
	fIsDevice = isDevice;
	fRole = role;
	fOptions = options;
}


// AddToPlan
status_t
InitializeJob::AddToPlan(PartitionPlanBuilder& plan, const BString& opID,
	PartitionOpIdMap& createdIds)
{
	// create a table only when fDiskSystem is a partitioning system
	bool isPartitioningSystem = false;
	user_disk_system_info info;
	if (_kern_find_disk_system(fDiskSystem, &info) == B_OK)
		isPartitioningSystem = (info.flags & B_DISK_SYSTEM_IS_FILE_SYSTEM) == 0;

	if (isPartitioningSystem) {
		// plan format knows "mbr"/"gpt", not Haiku short names
		const char* type = strcasecmp(fDiskSystem, "intel") == 0
			? "mbr" : fDiskSystem;
		plan.AddCreateTable(opID.String(), type);
		return B_OK;
	}

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

	// plan format says "fat32"/"swap", not "fat"/"linux-swap"
	const char* filesystem = fDiskSystem;
	if (strcasecmp(fDiskSystem, "fat") == 0)
		filesystem = "fat32";
	else if (strcasecmp(fDiskSystem, "linux-swap") == 0)
		filesystem = "swap";

	plan.AddFormat(opID.String(), targetRef.String(), filesystem, fName,
		fRole.String(), &fOptions);
	return B_OK;
}
