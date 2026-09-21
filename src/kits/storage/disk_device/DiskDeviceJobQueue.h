/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DISK_DEVICE_JOB_QUEUE_H
#define _DISK_DEVICE_JOB_QUEUE_H

#include <DiskDeviceDefs.h>
#include <ObjectList.h>


class BDiskDevice;
class BMessage;


namespace BPrivate {


class DiskDeviceJob;


class DiskDeviceJobQueue {
public:
								DiskDeviceJobQueue();
								~DiskDeviceJobQueue();

			status_t			AddJob(DiskDeviceJob* job);

			status_t			Execute();

			// one pkexec prompt, not one per job
			status_t			ExecuteViaHelper(BDiskDevice* device,
									BMessage* outResult = NULL);

private:
	typedef	BObjectList<DiskDeviceJob, true> JobList;

			JobList				fJobs;
};


}	// namespace BPrivate

using BPrivate::DiskDeviceJobQueue;

#endif	// _DISK_DEVICE_JOB_QUEUE_H
