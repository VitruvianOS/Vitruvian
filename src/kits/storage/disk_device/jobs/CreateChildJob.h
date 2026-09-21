/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CREATE_CHILD_JOB_H
#define _CREATE_CHILD_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


class CreateChildJob : public DiskDeviceJob {
public:

								CreateChildJob(PartitionReference* partition,
									PartitionReference* child);
	virtual						~CreateChildJob();

			status_t			Init(off_t offset, off_t size,
									const char* type, const char* name,
									const char* parameters);

			void				SetRole(const char* role);

	virtual	status_t			Do();
	virtual	status_t			AddToPlan(PartitionPlanBuilder& plan,
									const BString& opID,
									PartitionOpIdMap& createdIds);

protected:
			off_t				fOffset;
			off_t				fSize;
			char*				fType;
			char*				fName;
			char*				fParameters;
			BString				fRole;
};


}	// namespace BPrivate

using BPrivate::CreateChildJob;

#endif	// _CREATE_CHILD_JOB_H
