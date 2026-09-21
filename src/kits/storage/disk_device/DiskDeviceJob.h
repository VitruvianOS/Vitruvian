/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DISK_DEVICE_JOB_H
#define _DISK_DEVICE_JOB_H

#include <Message.h>
#include <String.h>
#include <SupportDefs.h>


namespace BPrivate {


class PartitionReference;
class PartitionPlanBuilder;
class PartitionOpIdMap;


class DiskDeviceJob {
public:
								DiskDeviceJob(PartitionReference* partition,
									PartitionReference* child = NULL);
	virtual						~DiskDeviceJob();

	virtual	status_t			Do() = 0;

	// createdIds maps ids for children created earlier in the commit
	virtual	status_t			AddToPlan(PartitionPlanBuilder& plan,
									const BString& opID,
									PartitionOpIdMap& createdIds);

protected:
			PartitionReference*	fPartition;
			PartitionReference*	fChild;
};


}	// namespace BPrivate

using BPrivate::DiskDeviceJob;

#endif	// _DISK_DEVICE_JOB_H
