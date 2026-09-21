/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "DiskDeviceJobQueue.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <typeinfo>

#include <DiskDevice.h>
#include <Path.h>

#include "DiskDeviceJob.h"
#include "PartitionPlanBuilder.h"


#undef TRACE
//#define TRACE(x...)
#define TRACE(x...)	printf(x)


// constructor
DiskDeviceJobQueue::DiskDeviceJobQueue()
	: fJobs(20)
{
}


// destructor
DiskDeviceJobQueue::~DiskDeviceJobQueue()
{
}


// AddJob
status_t
DiskDeviceJobQueue::AddJob(DiskDeviceJob* job)
{
	if (!job)
		return B_BAD_VALUE;

	return fJobs.AddItem(job) ? B_OK : B_NO_MEMORY;
}


// Execute
status_t
DiskDeviceJobQueue::Execute()
{
	int32 count = fJobs.CountItems();
	for (int32 i = 0; i < count; i++) {
		DiskDeviceJob* job = fJobs.ItemAt(i);

		TRACE("DiskDeviceJobQueue::Execute(): executing job: %s\n",
			typeid(*job).name());

		status_t error = job->Do();
		if (error != B_OK) {
			TRACE("DiskDeviceJobQueue::Execute(): executing job failed: %s\n",
				strerror(error));
			return error;
		}
	}

	return B_OK;
}


// ExecuteViaHelper
status_t
DiskDeviceJobQueue::ExecuteViaHelper(BDiskDevice* device, BMessage* outResult)
{
	int32 count = fJobs.CountItems();
	if (count == 0)
		return B_OK;

	BPath diskPath;
	status_t error = device->GetPath(&diskPath);
	if (error != B_OK)
		return error;

	PartitionPlanBuilder plan;
	plan.SetDisk(diskPath.Path());

	PartitionOpIdMap createdIds;
	for (int32 i = 0; i < count; i++) {
		DiskDeviceJob* job = fJobs.ItemAt(i);
		BString opID;
		opID << "j" << i;

		error = job->AddToPlan(plan, opID, createdIds);
		if (error != B_OK) {
			TRACE("DiskDeviceJobQueue::ExecuteViaHelper(): job %s has no "
				"plan-format mapping: %s\n", typeid(*job).name(),
				strerror(error));
			return error;
		}
	}

	if (!plan.IsValid()) {
		TRACE("DiskDeviceJobQueue::ExecuteViaHelper(): plan rejected "
			"locally (a field contained a newline)\n");
		return B_BAD_VALUE;
	}

	BMessage result;
	error = PartitionPlanBuilder::RunPlan(plan.ToPlan(), result, false);

	if (outResult != NULL)
		*outResult = result;

	BString status;
	result.FindString("status", &status);
	if (error != B_OK || status != "ok") {
		BString failedOp;
		result.FindString("failed_op", &failedOp);
		TRACE("DiskDeviceJobQueue::ExecuteViaHelper(): plan failed "
			"(status=%s, failed_op=%s)\n", status.String(),
			failedOp.String());
		return B_ERROR;
	}

	// helper rescan may lag; wait for reported devnodes to appear
	BMessage volumes;
	if (result.FindMessage("volumes", &volumes) == B_OK) {
		BMessage volume;
		for (int32 i = 0;
				volumes.FindMessage((BString() << i).String(), &volume) == B_OK;
				i++) {
			BString devnode;
			if (volume.FindString("devnode", &devnode) != B_OK
				|| devnode.IsEmpty()) {
				continue;
			}

			struct stat st;
			for (int32 attempt = 0; attempt < 40; attempt++) {
				if (stat(devnode.String(), &st) == 0)
					break;
				usleep(50000);
			}
		}
	}

	return B_OK;
}
