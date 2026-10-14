/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _UDEV_SCANNER_H
#define _UDEV_SCANNER_H


#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <SupportDefs.h>

#include <vector>

#include "HardwareModel.h"


// Owns the udev enumerate + monitor pair. The initial scan and every
// hotplug re-scan run on a dedicated thread; the UI pulls record
// snapshots under a lock and never touches udev itself. Pure libudev
// and sysfs, no libbe calls off the window thread except messaging.


class UdevScanner {
public:
							UdevScanner();
							~UdevScanner();

			status_t		Start(const BMessenger& listener);
			void			Stop();

			// Asks the worker to rescan; posts kMsgDevicesUpdated to
			// the listener when the snapshot is ready.
			void			RequestRescan();

			// Copy of the current snapshot.
			std::vector<Hardware::DeviceRecord>
							Records();

private:
			static int32	_ThreadEntry(void* data);
			void			_Run();
			void			_Scan();
			void			_Notify();

			BLocker			fLock;
			BMessenger		fListener;
			thread_id		fThread;
			int				fWakePipe[2];
	volatile bool			fQuitRequested;
	volatile bool			fRescanRequested;

			std::vector<Hardware::DeviceRecord>
							fRecords;
};


#endif	// _UDEV_SCANNER_H
