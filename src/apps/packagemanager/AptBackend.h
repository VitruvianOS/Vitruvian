/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef APT_BACKEND_H
#define APT_BACKEND_H

#include <Locker.h>
#include <ObjectList.h>
#include <String.h>

#include "PackageManagerDefs.h"


// fCancelRequested is written from the looper thread and read from the
// transaction thread, so it is guarded by fCancelLock.
class AptBackend {
public:
								AptBackend();
								~AptBackend();

			status_t			SimulateTransaction(
									const BObjectList<BString>& install,
									const BObjectList<BString>& remove,
									const BObjectList<BString>& purge,
									BString* summary);
			status_t			ApplyTransaction(
									const BObjectList<BString>& install,
									const BObjectList<BString>& remove,
									const BObjectList<BString>& purge);
			status_t			ConfigurePending();

			status_t			Update();
			status_t			Upgrade(bool full);

			status_t			SetChannel(package_channel channel);

			apt_error			LastError() const
									{ return fLastError; }
			const BString&		LastErrorDetail() const
									{ return fLastErrorDetail; }
			pid_t				LockHolderPid() const
									{ return fLockHolderPid; }
			const BString&		LockPath() const
									{ return fLockPath; }

			void				RequestCancel();

			// percent < 0 means no figure yet; package may be "".
			typedef void (*progress_hook)(int32 percent, const char* text,
									const char* package, void* cookie);
			void				SetProgressHook(progress_hook hook,
									void* cookie);

private:
			status_t			_RunQuery(const char* const argv[],
									BObjectList<BString, true>* lines);
			status_t			_RunQueryStreaming(const char* const argv[],
									void (*handler)(const BString& line,
										void* cookie),
									void* cookie);
	static	bool				_ReadFile(const char* path, BString* out);
			status_t			_RunVerb(const char* verb,
									const BObjectList<BString>& names);
			status_t			_RunHelper(const char* const argv[]);
			void				_ReportProgress(const BString& line);
			bool				_CheckDpkgLock();
			apt_error			_ClassifyOutput(const BString& output,
									int exitStatus);
			void				_SetError(apt_error error,
									const char* detail);
			bool				_CancelRequested();
			void				_ResetCancel();

			apt_error			fLastError;
			BString				fLastErrorDetail;
			pid_t				fLockHolderPid;
			BString				fLockPath;
			BLocker				fCancelLock;
			bool				fCancelRequested;
			progress_hook		fProgressHook;
			void*				fProgressCookie;
};


#endif // APT_BACKEND_H
