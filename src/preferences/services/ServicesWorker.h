/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_WORKER_H
#define SERVICES_WORKER_H

#include <Locker.h>
#include <Looper.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#include "ServicesDefs.h"
#include "ServicesUnit.h"


// systemd over D-Bus plus the journal, off the window thread.
class ServicesWorker : public BLooper {
	typedef BLooper Inherited;
public:
								ServicesWorker(const BMessenger& owner);
	virtual						~ServicesWorker();

	virtual	void				MessageReceived(BMessage* message);

private:
			bool				_Connect(const char* manager);
			void				_Disconnect();
			bool				_IsConnected() const
									{ return fBus != NULL; }

			void				_DoReload();
			void				_DoListUnits();
			void				_DoListUnitFiles();
			void				_PostUnitBatches();
			void				_DoUnitAction(BMessage* message);
			void				_DoLoadJournal(BMessage* message);

			// Reads live properties for one unit and posts kMsgUnitUpdated.
			void				_RefreshUnit(const BString& name);

			// Applies one PropertiesChanged dict entry to the unit model.
			void				_HandlePropertiesChanged(void* message,
									const BString& name);
			void				_ApplyUnitProperty(const BString& name,
									const char* key, const char* value);

			void				_PostUnit(const ServicesUnit& unit);
			void				_PostError(const char* operation,
									const char* detail);

			bool				_UsesUserManager() const
									{ return fUsesUser; }

	static	int32				_BusThreadEntry(void* self);
			int32				_BusLoop();
			void				_HandleSignal(void* message);

			// Unit object path for systemd's escaped unit naming.
	static	BString			_UnitPath(const BString& unit);

			BMessenger			fOwner;
			void*				fBus;			// sd_bus*
			thread_id			fThread;
			volatile bool		fBusRunning;
			bool				fUsesUser;
			// Serialises looper method calls against the bus thread.
			BLocker			fBusLock;
			// _BusLoop polls [0]; _Disconnect writes [1] to wake it.
			int				fWakePipe[2];

			// Owning: the list is the worker's model.
			BObjectList<ServicesUnit, true>	fUnits;
};


#endif // SERVICES_WORKER_H
