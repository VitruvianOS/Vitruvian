/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _INITIALIZE_JOB_H
#define _INITIALIZE_JOB_H

#include "DiskDeviceJob.h"


namespace BPrivate {


class InitializeJob : public DiskDeviceJob {
public:

								InitializeJob(PartitionReference* partition);
	virtual						~InitializeJob();

			status_t			Init(const char* diskSystem, const char* name,
									const char* parameters);

			void				SetPlanMetadata(bool isDevice,
									const char* role, const BMessage& options);

	virtual	status_t			Do();
	virtual	status_t			AddToPlan(PartitionPlanBuilder& plan,
									const BString& opID,
									PartitionOpIdMap& createdIds);

protected:
			char*				fDiskSystem;
			char*				fName;
			char*				fParameters;
			bool				fIsDevice;
			BString				fRole;
			BMessage			fOptions;
};


}	// namespace BPrivate

using BPrivate::InitializeJob;

#endif	// _INITIALIZE_JOB_H
