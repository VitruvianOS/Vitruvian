/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageWorker.h"

#include <new>

#include <Message.h>
#include <OS.h>

#include <ObjectList.h>
#include <String.h>

#include <package/DependencyExpression.h>
#include <package/PackageInfo.h>
#include <package/PackageRoster.h>

#include "AptBackend.h"
#include "PackageInfo.h"


// From the transaction thread; fTransactionRunning stays single-writer.
enum { kMsgWorkerJobDone = 'pwjd' };


static void
assign_category(PackageInfo* info)
{
	const BString& name = info->Name();
	if (name == "vos" || name == "nexus-dkms" || name.StartsWith("vos-")) {
		info->SetCategory("Vitruvian");
		return;
	}
	if (name.StartsWith("haiku-") || name.StartsWith("libhaiku")) {
		info->SetCategory("Haiku libs");
		return;
	}
	info->SetCategory(info->Section().Length() > 0
		? info->Section().String() : "Other");
}


PackageWorker::PackageWorker(const BMessenger& owner)
	:
	BLooper("package worker"),
	fOwner(owner),
	fBackend(new(std::nothrow) AptBackend()),
	fRoster(new(std::nothrow) VPackageRoster()),
	fHaveDetailReply(false),
	fBusy(false),
	fTransactionThread(-1),
	fTransactionRunning(false)
{
}


PackageWorker::~PackageWorker()
{
	if (fTransactionRunning) {
		// Backstop: wait out the transaction thread before freeing fBackend.
		status_t exitValue;
		wait_for_thread(fTransactionThread, &exitValue);
	}

	delete fBackend;
	delete fRoster;
}


void
PackageWorker::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshList:
			_DoRefreshList();
			break;

		case kMsgLoadDetails:
		{
			const char* name = NULL;
			if (message->FindString("name", &name) == B_OK)
				_DoLoadDetails(name);
			break;
		}

		case kMsgLoadLog:
			_DoLoadLog();
			break;

		case kMsgSimulate:
			_DoSimulate(message);
			break;

		case kMsgApplyChanges:
			_DoApplyChanges(message);
			break;

		case kMsgAptUpdate:
			_DoAptUpdate();
			break;

		case kMsgCancel:
			RequestCancel();
			break;

		case kVMsgPackageListReply:
			_OnPackageListReply(message);
			break;

		case kVMsgPackageDetailsReply:
			_OnPackageDetailsReply(message);
			break;

		case kVMsgPackageContentsReply:
			_OnPackageContentsReply(message);
			break;

		case kVMsgAptLogReply:
			_OnAptLogReply(message);
			break;

		case kVMsgChangelogReply:
			_OnChangelogReply(message);
			break;

		case kVMsgQueryError:
			_OnQueryError(message);
			break;

		case kMsgWorkerJobDone:
			fTransactionRunning = false;
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


void
PackageWorker::RequestCancel()
{
	if (fBackend != NULL)
		fBackend->RequestCancel();
	if (fRoster != NULL)
		fRoster->RequestCancel();
}


void
PackageWorker::_DoRefreshList()
{
	BMessage progress(kMsgProgress);
	progress.AddInt32("percent", -1);
	progress.AddString("status", "Reading package lists");
	fOwner.SendMessage(&progress);

	fRoster->InvalidateCache();
	fRoster->GetPackageList(BMessenger(this));
}


package_state
PackageWorker::_MapState(v_package_state state)
{
	switch (state) {
		case V_PACKAGE_STATE_INSTALLED:
			return kPackageInstalled;
		case V_PACKAGE_STATE_UPGRADABLE:
			return kPackageUpgradable;
		case V_PACKAGE_STATE_AVAILABLE:
			return kPackageAvailable;
		case V_PACKAGE_STATE_UNKNOWN:
		default:
			return kPackageAvailable;
	}
}


package_channel
PackageWorker::_MapChannel(v_package_channel channel)
{
	switch (channel) {
		case V_CHANNEL_STABLE:
			return kChannelStable;
		case V_CHANNEL_TESTING:
			return kChannelTesting;
		case V_CHANNEL_NIGHTLY:
			return kChannelNightly;
		case V_CHANNEL_DEBIAN:
			return kChannelDebian;
		case V_CHANNEL_UNKNOWN:
		default:
			return kChannelUnknown;
	}
}


apt_error
PackageWorker::_MapError(v_package_error error)
{
	switch (error) {
		case V_PACKAGE_ERROR_LOCK_HELD:
			return kAptErrorLockHeld;
		case V_PACKAGE_ERROR_BROKEN_DEPS:
			return kAptErrorBrokenDeps;
		case V_PACKAGE_ERROR_NETWORK:
			return kAptErrorNetwork;
		case V_PACKAGE_ERROR_SIGNATURE:
			return kAptErrorSignature;
		case V_PACKAGE_ERROR_DISK_FULL:
			return kAptErrorDiskFull;
		case V_PACKAGE_ERROR_NO_PRIVILEGE:
			return kAptErrorNoPrivilege;
		case V_PACKAGE_ERROR_INTERRUPTED:
			return kAptErrorInterrupted;
		case V_PACKAGE_ERROR_NONE:
			return kAptErrorNone;
		case V_PACKAGE_ERROR_UNKNOWN:
		default:
			return kAptErrorUnknown;
	}
}


PackageInfo*
PackageWorker::_BuildPackageInfo(const VPackageInfoRef& ref)
{
	PackageInfo* info = new PackageInfo(ref->Name().String());
	info->SetVersion(ref->InstalledVersion().String());
	info->SetCandidateVersion(ref->CandidateVersion().String());
	info->SetArchitecture(ref->Architecture().String());
	info->SetSection(ref->Section().String());
	info->SetSummary(ref->Summary().String());
	info->SetInstalledSize(ref->InstalledSize());
	info->SetDownloadSize(ref->DownloadSize());
	info->SetState(_MapState(ref->State()));
	info->SetChannel(_MapChannel(ref->Channel()));
	assign_category(info);
	return info;
}


void
PackageWorker::_OnPackageListReply(BMessage* message)
{
	// Non-owning: MainWindow::fPackages takes the pointers, frees the list.
	BObjectList<PackageInfo>* packages = new BObjectList<PackageInfo>(2048);

	void* pointer = NULL;
	for (int32 i = 0; message->FindPointer("package", i, &pointer) == B_OK;
			i++) {
		VPackageInfoRef ref((VPackageInfo*)pointer, true);
		packages->AddItem(_BuildPackageInfo(ref));
	}

	BMessage reply(kMsgListReady);
	reply.AddPointer("packages", packages);
	reply.AddInt32("count", packages->CountItems());
	fOwner.SendMessage(&reply);
}


BString
PackageWorker::_FormatDepends(
	const BObjectList<VDependencyExpression, true>& depends)
{
	BString text;
	int32 lastGroup = -1;

	for (int32 i = 0; i < depends.CountItems(); i++) {
		const VDependencyExpression* dep = depends.ItemAt(i);

		if (i > 0) {
			// Same nonzero Group() means alternatives of one "a | b" term.
			text << ((dep->Group() != 0 && dep->Group() == lastGroup)
				? " | " : ", ");
		}
		lastGroup = dep->Group();

		text << dep->Name();
		if (dep->Operator() != V_DEPENDENCY_OP_NONE) {
			const char* op = "";
			switch (dep->Operator()) {
				case V_DEPENDENCY_OP_LT: op = "<<"; break;
				case V_DEPENDENCY_OP_LE: op = "<="; break;
				case V_DEPENDENCY_OP_EQ: op = "="; break;
				case V_DEPENDENCY_OP_GE: op = ">="; break;
				case V_DEPENDENCY_OP_GT: op = ">>"; break;
				case V_DEPENDENCY_OP_NE: op = "!="; break;
				default: break;
			}
			text << " (" << op << " " << dep->Version().String() << ")";
		}
	}

	return text;
}


void
PackageWorker::_DoLoadDetails(const char* name)
{
	fPendingDetailNames.AddItem(new BString(name));
	fRoster->GetPackageInfo(name, BMessenger(this));
	fRoster->GetPackageContents(name, BMessenger(this));
	// The changelog reply carries its own "name"; no FIFO slot needed.
	fRoster->GetChangelog(name, BMessenger(this));
}


void
PackageWorker::_OnPackageDetailsReply(BMessage* message)
{
	void* pointer = NULL;
	if (message->FindPointer("package", &pointer) != B_OK)
		return;

	fPendingDetailInfo.SetTo((VPackageInfo*)pointer, true);
	fHaveDetailReply = true;
}


void
PackageWorker::_OnPackageContentsReply(BMessage* message)
{
	// Defensive against a details/contents desync corrupting the FIFO.
	if (!fHaveDetailReply || fPendingDetailNames.IsEmpty())
		return;

	BString* namePtr = fPendingDetailNames.RemoveItemAt(0);
	BString name(*namePtr);
	delete namePtr;

	const VPackageInfoRef& info = fPendingDetailInfo;

	BMessage reply(kMsgDetailsReady);
	// Name from the popped queue entry, not the reply, so MainWindow's
	// selection check cannot desync.
	const char* version = info->CandidateVersion().IsEmpty()
		? info->InstalledVersion().String() : info->CandidateVersion().String();

	reply.AddString("name", name);
	reply.AddString("description", info->Description());
	reply.AddString("version", version);
	reply.AddString("section", info->Section());
	reply.AddInt64("installed_size", (int64)info->InstalledSize());
	reply.AddString("depends", _FormatDepends(info->Depends()));
	reply.AddInt32("channel", (int32)_MapChannel(info->Channel()));

	const char* path = NULL;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK; i++)
		reply.AddString("path", path);

	fOwner.SendMessage(&reply);

	fPendingDetailInfo.Unset();
	fHaveDetailReply = false;
}


void
PackageWorker::_DoLoadLog()
{
	fRoster->GetAptLog(BMessenger(this));
}


void
PackageWorker::_OnAptLogReply(BMessage* message)
{
	BString text;
	message->FindString("text", &text);

	BMessage reply(kMsgLogReady);
	reply.AddString("text", text);
	fOwner.SendMessage(&reply);
}


void
PackageWorker::_OnChangelogReply(BMessage* message)
{
	BString name;
	message->FindString("name", &name);
	BString text;
	message->FindString("text", &text);

	BMessage reply(kMsgChangelogReady);
	reply.AddString("name", name);
	reply.AddString("text", text);
	fOwner.SendMessage(&reply);
}


void
PackageWorker::_OnQueryError(BMessage* message)
{
	int32 error = V_PACKAGE_ERROR_UNKNOWN;
	message->FindInt32("error", &error);
	BString detail;
	message->FindString("detail", &detail);

	_ReportError(_MapError((v_package_error)error), detail.String());
}


static void
collect_names(BMessage* request, const char* field,
	BObjectList<BString, true>* into)
{
	const char* name = NULL;
	for (int32 i = 0; request->FindString(field, i, &name) == B_OK; i++)
		into->AddItem(new BString(name));
}


static void
borrow(const BObjectList<BString, true>& owned, BObjectList<BString>* into)
{
	for (int32 i = 0; i < owned.CountItems(); i++)
		into->AddItem(owned.ItemAt(i));
}


static void
worker_progress(int32 percent, const char* text, const char* package,
	void* cookie)
{
	((PackageWorker*)cookie)->ReportProgress(percent, text, package);
}


// The lists are allocated by the looper, freed by the simulate thread.
struct SimulateJob {
	PackageWorker*					worker;
	BObjectList<BString, true>*	install;
	BObjectList<BString, true>*	remove;
	BObjectList<BString, true>*	purge;
};


void
PackageWorker::_DoSimulate(BMessage* request)
{
	// Simulate shares fBackend with apply/update, so it takes the same gate.
	if (fTransactionRunning) {
		_ReportError(kAptErrorUnknown, "A transaction is already running.");
		return;
	}

	SimulateJob* job = new(std::nothrow) SimulateJob;
	if (job == NULL) {
		_ReportError(kAptErrorUnknown, "Out of memory.");
		return;
	}
	job->worker = this;
	job->install = new BObjectList<BString, true>(16);
	job->remove = new BObjectList<BString, true>(16);
	job->purge = new BObjectList<BString, true>(16);
	collect_names(request, "install", job->install);
	collect_names(request, "remove", job->remove);
	collect_names(request, "purge", job->purge);

	fTransactionRunning = true;
	fTransactionThread = spawn_thread(_SimulateThread,
		"package simulate", B_NORMAL_PRIORITY, job);
	if (fTransactionThread < 0 || resume_thread(fTransactionThread) != B_OK) {
		fTransactionRunning = false;
		delete job->install;
		delete job->remove;
		delete job->purge;
		delete job;
		_ReportError(kAptErrorUnknown, "Could not start simulate thread.");
	}
}


void
PackageWorker::_RunSimulate(const BObjectList<BString, true>* installOwned,
	const BObjectList<BString, true>* removeOwned,
	const BObjectList<BString, true>* purgeOwned)
{
	BObjectList<BString> install;
	BObjectList<BString> remove;
	BObjectList<BString> purge;
	borrow(*installOwned, &install);
	borrow(*removeOwned, &remove);
	borrow(*purgeOwned, &purge);

	_ReportProgress(-1, "Resolving changes...", "");

	BString summary;
	if (fBackend->SimulateTransaction(install, remove, purge, &summary)
			!= B_OK) {
		_ReportError(fBackend->LastError(),
			fBackend->LastErrorDetail().String());
	} else {
		// Echo the exact simulated set; the request message is gone by
		// now, so rebuild from the owned lists.
		BMessage reply(kMsgSimulateReady);
		reply.AddString("summary", summary);
		for (int32 i = 0; i < installOwned->CountItems(); i++)
			reply.AddString("install", *installOwned->ItemAt(i));
		for (int32 i = 0; i < removeOwned->CountItems(); i++)
			reply.AddString("remove", *removeOwned->ItemAt(i));
		for (int32 i = 0; i < purgeOwned->CountItems(); i++)
			reply.AddString("purge", *purgeOwned->ItemAt(i));
		fOwner.SendMessage(&reply);
	}

	BMessenger(this).SendMessage(kMsgWorkerJobDone);
}


int32
PackageWorker::_SimulateThread(void* data)
{
	SimulateJob* job = (SimulateJob*)data;
	job->worker->_RunSimulate(job->install, job->remove, job->purge);
	delete job->install;
	delete job->remove;
	delete job->purge;
	delete job;
	return 0;
}


// The lists are allocated by the looper, freed by the transaction thread.
struct ApplyChangesJob {
	PackageWorker*					worker;
	BObjectList<BString, true>*	install;
	BObjectList<BString, true>*	remove;
	BObjectList<BString, true>*	purge;
};


void
PackageWorker::_DoApplyChanges(BMessage* request)
{
	// The window already gates kMsgApply; this looper checks for itself.
	if (fTransactionRunning) {
		_ReportError(kAptErrorUnknown, "A transaction is already running.");
		return;
	}

	ApplyChangesJob* job = new(std::nothrow) ApplyChangesJob;
	if (job == NULL) {
		_ReportError(kAptErrorUnknown, "Out of memory.");
		return;
	}
	job->worker = this;
	job->install = new BObjectList<BString, true>(16);
	job->remove = new BObjectList<BString, true>(16);
	job->purge = new BObjectList<BString, true>(16);
	collect_names(request, "install", job->install);
	collect_names(request, "remove", job->remove);
	collect_names(request, "purge", job->purge);

	fTransactionRunning = true;
	fTransactionThread = spawn_thread(_ApplyChangesThread,
		"package apply changes", B_NORMAL_PRIORITY, job);
	if (fTransactionThread < 0 || resume_thread(fTransactionThread) != B_OK) {
		fTransactionRunning = false;
		delete job->install;
		delete job->remove;
		delete job->purge;
		delete job;
		_ReportError(kAptErrorUnknown, "Could not start transaction thread.");
	}
}


void
PackageWorker::_RunApplyChanges(const BObjectList<BString, true>* installOwned,
	const BObjectList<BString, true>* removeOwned,
	const BObjectList<BString, true>* purgeOwned)
{
	BObjectList<BString> install;
	BObjectList<BString> remove;
	BObjectList<BString> purge;
	borrow(*installOwned, &install);
	borrow(*removeOwned, &remove);
	borrow(*purgeOwned, &purge);

	_ReportProgress(-1, "Applying changes...", "");

	fBackend->SetProgressHook(worker_progress, this);
	status_t result = fBackend->ApplyTransaction(install, remove, purge);
	fBackend->SetProgressHook(NULL, NULL);

	if (result != B_OK) {
		_ReportError(fBackend->LastError(),
			fBackend->LastErrorDetail().String());
	} else {
		BMessage done(kMsgTransactionDone);
		fOwner.SendMessage(&done);
	}

	BMessenger(this).SendMessage(kMsgWorkerJobDone);
}


int32
PackageWorker::_ApplyChangesThread(void* data)
{
	ApplyChangesJob* job = (ApplyChangesJob*)data;
	job->worker->_RunApplyChanges(job->install, job->remove, job->purge);
	delete job->install;
	delete job->remove;
	delete job->purge;
	delete job;
	return 0;
}


void
PackageWorker::_DoAptUpdate()
{
	if (fTransactionRunning) {
		_ReportError(kAptErrorUnknown, "A transaction is already running.");
		return;
	}

	fTransactionRunning = true;
	fTransactionThread = spawn_thread(_AptUpdateThread,
		"package apt update", B_NORMAL_PRIORITY, this);
	if (fTransactionThread < 0 || resume_thread(fTransactionThread) != B_OK) {
		fTransactionRunning = false;
		_ReportError(kAptErrorUnknown, "Could not start update thread.");
	}
}


void
PackageWorker::_RunAptUpdate()
{
	_ReportProgress(-1, "Updating package lists...", "");

	fBackend->SetProgressHook(worker_progress, this);
	status_t result = fBackend->Update();
	fBackend->SetProgressHook(NULL, NULL);

	if (result != B_OK) {
		_ReportError(fBackend->LastError(),
			fBackend->LastErrorDetail().String());
	} else {
		BMessage done(kMsgTransactionDone);
		fOwner.SendMessage(&done);
	}

	BMessenger(this).SendMessage(kMsgWorkerJobDone);
}


int32
PackageWorker::_AptUpdateThread(void* data)
{
	((PackageWorker*)data)->_RunAptUpdate();
	return 0;
}


void
PackageWorker::_ReportProgress(int32 percent, const char* status,
	const char* package)
{
	BMessage message(kMsgProgress);
	message.AddInt32("percent", percent);
	message.AddString("status", status);
	if (package != NULL)
		message.AddString("package", package);

	fOwner.SendMessage(&message);
}


void
PackageWorker::_ReportError(apt_error error, const char* detail)
{
	BMessage message(kMsgError);
	message.AddInt32("error", (int32)error);
	message.AddString("detail", detail);
	if (error == kAptErrorLockHeld && fBackend != NULL) {
		message.AddInt32("pid", (int32)fBackend->LockHolderPid());
		message.AddString("lock_path", fBackend->LockPath());
	}

	fOwner.SendMessage(&message);
}


void
PackageWorker::_ReportDone(status_t status, const char* detail)
{
	fBusy = false;

	BMessage message(kMsgTransactionDone);
	message.AddInt32("status", (int32)status);
	message.AddString("detail", detail);
	fOwner.SendMessage(&message);
}
