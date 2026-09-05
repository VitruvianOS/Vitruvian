/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_WORKER_H
#define PACKAGE_WORKER_H

#include <Looper.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <OS.h>
#include <String.h>

#include <package/PackageDefs.h>
#include <package/PackageInfo.h>

#include "PackageManagerDefs.h"


class AptBackend;
class PackageInfo;
class VDependencyExpression;
class VPackageRoster;


// Transactions block in waitpid() for as long as apt takes, so they run
// on a spawned thread; inline they would freeze the looper and make
// kMsgCancel undeliverable. fTransactionRunning and fTransactionThread
// are looper-thread only; replies go straight from the transaction
// thread to fOwner (SendMessage() is thread-safe).
class PackageWorker : public BLooper {
public:
								PackageWorker(const BMessenger& owner);
	virtual						~PackageWorker();

	virtual	void				MessageReceived(BMessage* message);

			void				RequestCancel();

			void				ReportProgress(int32 percent,
									const char* status, const char* package)
									{ _ReportProgress(percent, status, package); }

private:
			void				_DoRefreshList();
			void				_DoLoadDetails(const char* name);
			void				_DoLoadLog();
			void				_DoSimulate(BMessage* request);
			void				_DoApplyChanges(BMessage* request);
			void				_DoAptUpdate();

			void				_RunSimulate(
									const BObjectList<BString, true>* install,
									const BObjectList<BString, true>* remove,
									const BObjectList<BString, true>* purge);
			void				_RunApplyChanges(
									const BObjectList<BString, true>* install,
									const BObjectList<BString, true>* remove,
									const BObjectList<BString, true>* purge);
			void				_RunAptUpdate();

	static	int32				_SimulateThread(void* data);
	static	int32				_ApplyChangesThread(void* data);
	static	int32				_AptUpdateThread(void* data);

			void				_OnPackageListReply(BMessage* message);
			void				_OnPackageDetailsReply(BMessage* message);
			void				_OnPackageContentsReply(BMessage* message);
			void				_OnAptLogReply(BMessage* message);
			void				_OnChangelogReply(BMessage* message);
			void				_OnQueryError(BMessage* message);

			void				_ReportProgress(int32 percent,
									const char* status,
									const char* package = NULL);
			void				_ReportError(apt_error error,
									const char* detail);
			void				_ReportDone(status_t status,
									const char* detail);

	static	PackageInfo*		_BuildPackageInfo(const VPackageInfoRef& ref);
	static	BString				_FormatDepends(
									const BObjectList<VDependencyExpression,
										true>& depends);
	static	package_channel		_MapChannel(v_package_channel channel);
	static	package_state		_MapState(v_package_state state);
	static	apt_error			_MapError(v_package_error error);

			BMessenger			fOwner;
			AptBackend*			fBackend;
			VPackageRoster*		fRoster;
			// FIFO pairing the separate details/contents replies with their
			// request, by order.
			BObjectList<BString, true> fPendingDetailNames;
			VPackageInfoRef		fPendingDetailInfo;
			bool				fHaveDetailReply;
			bool				fBusy;

			// Looper-thread-only; see the class comment above.
			thread_id			fTransactionThread;
			bool				fTransactionRunning;
};


#endif // PACKAGE_WORKER_H
