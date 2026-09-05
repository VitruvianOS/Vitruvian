/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <package/PackageRoster.h>

#include <Looper.h>
#include <Message.h>

#include "AptCacheAdapter.h"
#include <package/PackageInfo.h>


// Internal request codes, never seen outside this file; each is
// handled on the worker thread and answered to the request's
// "replyTo" messenger.
enum {
	kDoGetPackageList		= 'vwGL',
	kDoGetUpgradable		= 'vwGU',
	kDoGetPackageInfo		= 'vwGI',
	kDoGetPackageContents	= 'vwGC',
	kDoGetAptLog			= 'vwGG',
	kDoGetChangelog			= 'vwGH',
	kDoInvalidateCache		= 'vwIC'
};


class VPackageRoster::Worker : public BLooper {
public:
								Worker();
	virtual						~Worker();

	virtual	void				MessageReceived(BMessage* message);

			bool				fCancelRequested;

private:
			void				_DoGetPackageList(const BMessenger& replyTo,
									bool upgradableOnly);
			void				_DoGetPackageInfo(const char* name,
									const BMessenger& replyTo);
			void				_DoGetPackageContents(const char* name,
									const BMessenger& replyTo);
			void				_DoGetAptLog(const BMessenger& replyTo);
			void				_DoGetChangelog(const char* name,
										const BMessenger& replyTo);

			VPackageInfo*		_BuildInfo(const apt_raw_package& raw);
			void				_ReplyError(const BMessenger& replyTo,
									status_t status,
									const char* name = NULL);

			AptCacheAdapter		fAdapter;
};


VPackageRoster::Worker::Worker()
	:
	BLooper("package roster worker"),
	fCancelRequested(false)
{
	Run();
}


VPackageRoster::Worker::~Worker()
{
}


VPackageInfo*
VPackageRoster::Worker::_BuildInfo(const apt_raw_package& raw)
{
	VPackageInfo* info = new VPackageInfo(raw.name.String());
	info->SetArchitecture(raw.architecture.String());
	info->SetInstalledVersion(raw.installedVersion.String());
	info->SetCandidateVersion(raw.candidateVersion.String());
	info->SetSection(raw.section.String());
	info->SetSummary(raw.summary.String());
	info->SetDescription(raw.description.String());
	info->SetInstalledSize(raw.installedSize);
	info->SetDownloadSize(raw.downloadSize);
	info->SetState(raw.state);
	info->SetChannel(raw.channel);
	if (raw.depends.Length() > 0)
		info->SetDepends(raw.depends.String());
	if (raw.recommends.Length() > 0)
		info->SetRecommends(raw.recommends.String());
	if (raw.breaks.Length() > 0)
		info->SetBreaks(raw.breaks.String());
	if (raw.conflicts.Length() > 0)
		info->SetConflicts(raw.conflicts.String());
	if (raw.provides.Length() > 0)
		info->SetProvides(raw.provides.String());
	return info;
}


void
VPackageRoster::Worker::_ReplyError(const BMessenger& replyTo,
	status_t status, const char* name)
{
	BMessage reply(kVMsgQueryError);
	reply.AddInt32("status", status);
	reply.AddInt32("error", (int32)V_PACKAGE_ERROR_UNKNOWN);
	reply.AddString("detail", strerror(status));
	if (name != NULL)
		reply.AddString("name", name);
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::_DoGetPackageList(const BMessenger& replyTo,
	bool upgradableOnly)
{
	BObjectList<apt_raw_package, true> raw(2048);
	status_t status = fAdapter.GetPackageList(&raw);
	if (status != B_OK) {
		_ReplyError(replyTo, status);
		return;
	}

	BMessage reply(kVMsgPackageListReply);
	reply.AddInt32("status", B_OK);
	for (int32 i = 0; i < raw.CountItems() && !fCancelRequested; i++) {
		const apt_raw_package* item = raw.ItemAt(i);
		if (upgradableOnly && item->state != V_PACKAGE_STATE_UPGRADABLE)
			continue;
		// Ownership transfers to whoever FindPointer()s this back out.
		reply.AddPointer("package", _BuildInfo(*item));
	}
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::_DoGetPackageInfo(const char* name,
	const BMessenger& replyTo)
{
	apt_raw_package raw;
	status_t status = fAdapter.GetPackageDetails(name, &raw);
	if (status != B_OK) {
		_ReplyError(replyTo, status, name);
		return;
	}

	BMessage reply(kVMsgPackageDetailsReply);
	reply.AddInt32("status", B_OK);
	reply.AddPointer("package", _BuildInfo(raw));
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::_DoGetPackageContents(const char* name,
	const BMessenger& replyTo)
{
	BObjectList<BString, true> paths(256);
	status_t status = fAdapter.GetPackageContents(name, &paths);
	if (status != B_OK) {
		_ReplyError(replyTo, status, name);
		return;
	}

	BMessage reply(kVMsgPackageContentsReply);
	reply.AddInt32("status", B_OK);
	for (int32 i = 0; i < paths.CountItems(); i++)
		reply.AddString("path", *paths.ItemAt(i));
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::_DoGetAptLog(const BMessenger& replyTo)
{
	BString text;
	status_t status = fAdapter.GetAptLog(&text);
	if (status != B_OK) {
		_ReplyError(replyTo, status);
		return;
	}

	BMessage reply(kVMsgAptLogReply);
	reply.AddInt32("status", B_OK);
	reply.AddString("text", text);
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::_DoGetChangelog(const char* name,
	const BMessenger& replyTo)
{
	BString text;
	status_t status = fAdapter.GetChangelog(name, &text);
	if (status != B_OK) {
		_ReplyError(replyTo, status, name);
		return;
	}

	BMessage reply(kVMsgChangelogReply);
	reply.AddInt32("status", B_OK);
	reply.AddString("name", name);
	reply.AddString("text", text);
	replyTo.SendMessage(&reply);
}


void
VPackageRoster::Worker::MessageReceived(BMessage* message)
{
	BMessenger replyTo;
	message->FindMessenger("replyTo", &replyTo);

	switch (message->what) {
		case kDoGetPackageList:
			fCancelRequested = false;
			_DoGetPackageList(replyTo, false);
			break;

		case kDoGetUpgradable:
			fCancelRequested = false;
			_DoGetPackageList(replyTo, true);
			break;

		case kDoGetPackageInfo:
		{
			BString name;
			message->FindString("name", &name);
			_DoGetPackageInfo(name.String(), replyTo);
			break;
		}

		case kDoGetPackageContents:
		{
			BString name;
			message->FindString("name", &name);
			_DoGetPackageContents(name.String(), replyTo);
			break;
		}

		case kDoGetAptLog:
			_DoGetAptLog(replyTo);
			break;

		case kDoGetChangelog:
		{
			BString name;
			message->FindString("name", &name);
			_DoGetChangelog(name.String(), replyTo);
			break;
		}

		case kDoInvalidateCache:
			fAdapter.InvalidateCache();
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


VPackageRoster::VPackageRoster()
	:
	fWorker(new Worker())
{
}


VPackageRoster::~VPackageRoster()
{
	if (fWorker->Lock())
		fWorker->Quit();
}


status_t
VPackageRoster::GetPackageList(const BMessenger& replyTo)
{
	BMessage request(kDoGetPackageList);
	request.AddMessenger("replyTo", replyTo);
	return fWorker->PostMessage(&request);
}


status_t
VPackageRoster::GetUpgradable(const BMessenger& replyTo)
{
	BMessage request(kDoGetUpgradable);
	request.AddMessenger("replyTo", replyTo);
	return fWorker->PostMessage(&request);
}


status_t
VPackageRoster::GetPackageInfo(const char* name, const BMessenger& replyTo)
{
	if (name == NULL)
		return B_BAD_VALUE;
	BMessage request(kDoGetPackageInfo);
	request.AddMessenger("replyTo", replyTo);
	request.AddString("name", name);
	return fWorker->PostMessage(&request);
}


status_t
VPackageRoster::GetPackageContents(const char* name,
	const BMessenger& replyTo)
{
	if (name == NULL)
		return B_BAD_VALUE;
	BMessage request(kDoGetPackageContents);
	request.AddMessenger("replyTo", replyTo);
	request.AddString("name", name);
	return fWorker->PostMessage(&request);
}


status_t
VPackageRoster::GetAptLog(const BMessenger& replyTo)
{
	BMessage request(kDoGetAptLog);
	request.AddMessenger("replyTo", replyTo);
	return fWorker->PostMessage(&request);
}


status_t
VPackageRoster::GetChangelog(const char* name, const BMessenger& replyTo)
{
	if (name == NULL)
		return B_BAD_VALUE;
	BMessage request(kDoGetChangelog);
	request.AddMessenger("replyTo", replyTo);
	request.AddString("name", name);
	return fWorker->PostMessage(&request);
}


void
VPackageRoster::RequestCancel()
{
	fWorker->fCancelRequested = true;
}


status_t
VPackageRoster::InvalidateCache()
{
	BMessage request(kDoInvalidateCache);
	return fWorker->PostMessage(&request);
}
