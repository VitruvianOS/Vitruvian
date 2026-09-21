/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SET_FLAGS_JOB_H
#define _SET_FLAGS_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


class SetFlagsJob : public DiskDeviceJob {
public:
								SetFlagsJob(PartitionReference* partition,
									PartitionReference* child);
	virtual						~SetFlagsJob();

			status_t			Init(const BMessage& flags);

	virtual	status_t			Do();
	virtual	status_t			AddToPlan(PartitionPlanBuilder& plan,
									const BString& opID,
									PartitionOpIdMap& createdIds);

private:
			BMessage			fFlags;
};


}

using BPrivate::SetFlagsJob;

#endif
