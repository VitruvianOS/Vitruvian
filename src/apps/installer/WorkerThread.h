/*
 * Copyright 2024, Vitruvian OS.
 * Based on Haiku Installer, Copyright 2005-2009, various authors.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Looper.h>
#include <Messenger.h>
#include <Partition.h>
#include <Volume.h>

class BMenu;
class ProgressReporter;

class WorkerThread : public BLooper {
public:
								WorkerThread(const BMessenger& owner);

	virtual	void				MessageReceived(BMessage* message);

			void				ScanDisksPartitions(BMenu* srcMenu,
									BMenu* dstMenu);

			bool				Cancel();
			void				SetLock(sem_id cancelSemaphore)
									{ fCancelSemaphore = cancelSemaphore; }

			void				StartInstall(partition_id sourcePartitionID,
									partition_id targetPartitionID);

private:
			status_t			_PerformInstall(partition_id sourcePartitionID,
									partition_id targetPartitionID);
			status_t			_InstallationError(status_t error);

			status_t			_GenerateFstab(const BPath& targetPath,
									const char* rootUUID,
									const char* rootFSType);
			status_t			_InstallGRUB(const BPath& targetPath,
									const char* diskDevice);
			status_t			_ConfigureSystem(const BPath& targetPath);

			void				_SetStatusMessage(const char* status);

private:
			class EntryFilter;

private:
			BMessenger			fOwner;
			BDiskDeviceRoster	fDDRoster;
			sem_id				fCancelSemaphore;
};

#endif // WORKER_THREAD_H
