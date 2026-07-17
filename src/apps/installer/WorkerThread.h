/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005, Jérôme DUVAL.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Looper.h>
#include <Messenger.h>
#include <Partition.h>
#include <String.h>
#include <Volume.h>


class BMenu;


class WorkerThread : public BLooper {
public:
								WorkerThread(const BMessenger& owner);
	virtual						~WorkerThread();

	virtual	void				MessageReceived(BMessage* message);

			void				ScanDisksPartitions(BMenu* dstMenu);

			bool				Cancel();
			void				SetLock(sem_id cancelSemaphore)
									{ fCancelSemaphore = cancelSemaphore; }
			// Scrubbed on destruction and after the helper runs.
			void				SetSetupConf(const BString& conf);

			void				SetInPlace(bool inPlace)
									{ fInPlace = inPlace; }

			void				StartInstall(partition_id targetPartitionID);

private:
			status_t			_PerformInstall(partition_id targetPartitionID);
			status_t			_PerformInPlaceInstall();
			status_t			_RunHelper(const char* mountPoint);
			status_t			_CommitSetup(const char* target);
			status_t			_InstallationError(status_t error);
			void				_SetStatusMessage(const char* status);

private:
			BMessenger			fOwner;
			BDiskDeviceRoster	fDDRoster;
			sem_id				fCancelSemaphore;
			BString				fSetupConf;
			bool				fInPlace;
};


#endif // WORKER_THREAD_H
