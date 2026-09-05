/*
 * Copyright 2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _V_PACKAGE_ROSTER_H
#define _V_PACKAGE_ROSTER_H


#include <Messenger.h>
#include <String.h>
#include <SupportDefs.h>

#include <package/PackageDefs.h>


// The kit's read entry point: every query is request-in/reply-out via
// a BMessenger, never a synchronous container return. Each reply
// (codes in PackageDefs.h) carries int32 "status"; on failure it is
// kVMsgQueryError with int32 "error" and BString "detail", plus
// BString "name" when the failed request named a package. On success:
//   kVMsgPackageListReply: repeated B_POINTER "package"; ownership of
//     each reference transfers to the receiver (wrap in VPackageInfoRef
//     or ReleaseReference() it).
//   kVMsgPackageDetailsReply: single B_POINTER "package", same ownership.
//   kVMsgPackageContentsReply: repeated BString "path".
//   kVMsgAptLogReply: BString "text".
//   kVMsgChangelogReply: BString "name" (echoed, so several in-flight
//     requests can be matched without a FIFO) and "text".
class VPackageRoster {
public:
								VPackageRoster();
	virtual						~VPackageRoster();

			status_t			GetPackageList(const BMessenger& replyTo);
			status_t			GetUpgradable(const BMessenger& replyTo);
			status_t			GetPackageInfo(const char* name,
									const BMessenger& replyTo);
			status_t			GetPackageContents(const char* name,
									const BMessenger& replyTo);
			status_t			GetAptLog(const BMessenger& replyTo);
			// Not an error when no changelog exists; the reply's "text"
			// says so in plain language.
			status_t			GetChangelog(const char* name,
										const BMessenger& replyTo);

			// Honoured between packages only; never interrupts a running child.
			void				RequestCancel();
			status_t			InvalidateCache();

private:
			class Worker;

			Worker*				fWorker;
};


#endif	// _V_PACKAGE_ROSTER_H
