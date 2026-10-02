/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _WIPE_JOB_H
#define _WIPE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


class WipeJob : public DiskDeviceJob {
public:
								WipeJob(PartitionReference* partition,
									bool full);
	virtual						~WipeJob();

	virtual	status_t			Do();
	virtual	status_t			AddToPlan(PartitionPlanBuilder& plan,
									const BString& opID,
									PartitionOpIdMap& createdIds);

private:
			bool				fFull;
};


}	// namespace BPrivate

using BPrivate::WipeJob;

#endif	// _WIPE_JOB_H
