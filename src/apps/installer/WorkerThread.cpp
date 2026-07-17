/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005-2008, Jérôme DUVAL.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "WorkerThread.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Catalog.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceTypes.h>
#include <DiskDeviceVisitor.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <String.h>

#include "CopyEngine.h"
#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "ProgressReporter.h"


#define B_TRANSLATION_CONTEXT "InstallProgress"


static const char* kInstallHelper = "/usr/libexec/vos-install-helper";
static const char* kSourceRoot = "/";
static const char* kTargetMount = "/target";


class TargetVisitor : public BDiskDeviceVisitor {
public:
	TargetVisitor(BMenu* menu)
		: fMenu(menu)
	{
	}

	virtual bool Visit(BDiskDevice* /*device*/)
	{
		return false;
	}

	virtual bool Visit(BPartition* partition, int32 /*level*/)
	{
		if (partition == NULL || partition->ContentSize() <= 0)
			return false;
		BPath path;
		if (partition->GetPath(&path) != B_OK)
			return false;
		if (partition->IsMounted())
			return false;
		const char* type = partition->Type();
		if (type != NULL) {
			// swap / ESP / BIOS boot — never install-target candidates.
			if (strcmp(type, "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f") == 0
				|| strcmp(type, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") == 0
				|| strcasecmp(type, "EFI") == 0
				|| strcmp(type, "21686148-6449-6e6f-744e-656564454649") == 0)
				return false;
		}
		if (partition->ContentSize() < (1LL << 30))
			return false;
		BString label;
		if (partition->ContentName() != NULL && *partition->ContentName())
			label.SetToFormat("%s (%s)", partition->ContentName(),
				path.Path());
		else
			label.SetTo(path.Path());
		PartitionMenuItem* item = new PartitionMenuItem(
			partition->ContentName(), label.String(), NULL,
			new BMessage(TARGET_PARTITION), partition->ID());
		fMenu->AddItem(item);
		return false;
	}

private:
	BMenu* fMenu;
};


WorkerThread::WorkerThread(const BMessenger& owner)
	:
	BLooper("WorkerThread"),
	fOwner(owner),
	fCancelSemaphore(-1),
	fInPlace(false)
{
	Run();
}


WorkerThread::~WorkerThread()
{
	if (fSetupConf.Length() > 0) {
		explicit_bzero(fSetupConf.LockBuffer(0), fSetupConf.Length());
		fSetupConf.UnlockBuffer(0);
	}
}


void
WorkerThread::SetSetupConf(const BString& conf)
{
	if (fSetupConf.Length() > 0) {
		explicit_bzero(fSetupConf.LockBuffer(0), fSetupConf.Length());
		fSetupConf.UnlockBuffer(0);
	}
	fSetupConf = conf;
}


void
WorkerThread::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_START_INSTALLING:
		{
			if (fInPlace) {
				_PerformInPlaceInstall();
			} else {
				partition_id target = -1;
				message->FindInt32("target", &target);
				_PerformInstall(target);
			}
			break;
		}
		default:
			BLooper::MessageReceived(message);
	}
}


void
WorkerThread::ScanDisksPartitions(BMenu* dstMenu)
{
	fDDRoster.RewindDevices();
	BDiskDevice device;
	while (fDDRoster.GetNextDevice(&device) == B_OK) {
		TargetVisitor visitor(dstMenu);
		device.VisitEachDescendant(&visitor);
	}
}


bool
WorkerThread::Cancel()
{
	if (fCancelSemaphore < 0)
		return false;
	release_sem(fCancelSemaphore);
	return true;
}


void
WorkerThread::StartInstall(partition_id targetPartitionID)
{
	BMessage msg(MSG_START_INSTALLING);
	msg.AddInt32("target", (int32)targetPartitionID);
	PostMessage(&msg);
}


status_t
WorkerThread::_PerformInstall(partition_id targetPartitionID)
{
	if (targetPartitionID < 0)
		return _InstallationError(B_BAD_VALUE);

	_SetStatusMessage(B_TRANSLATE("Preparing target..."));

	BDiskDevice device;
	BPartition* partition;
	status_t err = fDDRoster.GetPartitionWithID(targetPartitionID, &device,
		&partition);
	if (err != B_OK)
		return _InstallationError(err);

	BPath mountPath;
	if (partition->IsMounted()) {
		if (partition->GetMountPoint(&mountPath) != B_OK)
			return _InstallationError(B_ERROR);
	} else {
		if (partition->Mount(kTargetMount, 0) != B_OK)
			return _InstallationError(B_ERROR);
		mountPath.SetTo(kTargetMount);
	}

	_SetStatusMessage(B_TRANSLATE("Copying files..."));

	BMessage progressMsg(MSG_STATUS_MESSAGE);
	ProgressReporter reporter(fOwner, &progressMsg);
	CopyEngine engine(&reporter, fCancelSemaphore);
	err = engine.Copy(kSourceRoot, mountPath.Path());
	if (err != B_OK)
		return _InstallationError(err);

	_SetStatusMessage(B_TRANSLATE("Configuring target..."));
	err = _RunHelper(mountPath.Path());
	if (err != B_OK)
		return _InstallationError(err);

	_SetStatusMessage(B_TRANSLATE("Creating user account..."));
	err = _CommitSetup(mountPath.Path());
	if (err != B_OK)
		return _InstallationError(err);

	_SetStatusMessage(B_TRANSLATE("Installation complete."));
	BMessage finished(MSG_INSTALL_FINISHED);
	fOwner.SendMessage(&finished);
	return B_OK;
}


status_t
WorkerThread::_PerformInPlaceInstall()
{
	_SetStatusMessage(B_TRANSLATE("Creating user account..."));
	status_t err = _CommitSetup("/");
	if (err != B_OK)
		return _InstallationError(err);

	_SetStatusMessage(B_TRANSLATE("Setup complete."));
	BMessage finished(MSG_INSTALL_FINISHED);
	fOwner.SendMessage(&finished);
	return B_OK;
}


status_t
WorkerThread::_RunHelper(const char* mountPoint)
{
	pid_t pid = fork();
	if (pid < 0)
		return B_ERROR;
	if (pid == 0) {
		execl("/usr/bin/pkexec", "pkexec", kInstallHelper, mountPoint,
			(char*)NULL);
		_exit(127);
	}
	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
		return B_ERROR;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return B_OK;
	fprintf(stderr, "WorkerThread: vos-install-helper exit=%d\n", status);
	return B_ERROR;
}


status_t
WorkerThread::_CommitSetup(const char* target)
{
	if (fSetupConf.Length() == 0)
		return B_BAD_VALUE;

	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) < 0)
		return B_ERROR;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return B_ERROR;
	}
	if (pid == 0) {
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		close(pipefd[1]);
		execl("/usr/bin/pkexec", "pkexec", kInstallHelper,
			"--commit-setup", target, (char*)NULL);
		_exit(127);
	}

	close(pipefd[0]);
	const char* buf = fSetupConf.String();
	size_t left = fSetupConf.Length();
	while (left > 0) {
		ssize_t n = write(pipefd[1], buf, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(pipefd[1]);
			waitpid(pid, NULL, 0);
			return B_ERROR;
		}
		buf += n;
		left -= n;
	}
	close(pipefd[1]);

	explicit_bzero(fSetupConf.LockBuffer(0), fSetupConf.Length());
	fSetupConf.UnlockBuffer(0);
	fSetupConf.Truncate(0);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return B_OK;
	fprintf(stderr, "WorkerThread: setup helper exit=%d\n", status);
	return B_ERROR;
}


status_t
WorkerThread::_InstallationError(status_t error)
{
	BMessage msg(MSG_RESET);
	msg.AddInt32("error", error);
	fOwner.SendMessage(&msg);
	return error;
}


void
WorkerThread::_SetStatusMessage(const char* status)
{
	BMessage msg(MSG_STATUS_MESSAGE);
	msg.AddString("status", status);
	fOwner.SendMessage(&msg);
}
