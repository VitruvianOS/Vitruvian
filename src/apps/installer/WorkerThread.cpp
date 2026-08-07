/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005-2008, Jérôme DUVAL.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "WorkerThread.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <Catalog.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceTypes.h>
#include <DiskDeviceVisitor.h>
#include <Alert.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <String.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "ProgressReporter.h"


#define B_TRANSLATION_CONTEXT "InstallProgress"


static const char* kInstallHelper = "/usr/libexec/vos-install-helper";

static const bigtime_t kCancelKillDeadlineUsec = 5 * 1000000LL;
static const long      kWaitpidPollNsec        = 50 * 1000000L;
static const int       kCancelPollMs           = 200;


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

	// TODO: disable unsupported/mounted/boot partitions in the menu with a
	// reason instead of silently skipping them (see Haiku's PartitionMenuItem).
	static bool _IsSupportedTargetFs(const char* contentType)
	{
		static const char* const kSupportedFs[] = {
			"ext4", "ext3", "ext2"
		};
		if (contentType == NULL)
			return false;
		for (size_t i = 0; i < sizeof(kSupportedFs) / sizeof(kSupportedFs[0]); i++) {
			if (strcasecmp(contentType, kSupportedFs[i]) == 0)
				return true;
		}
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
			if (strcmp(type, "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f") == 0
				|| strcmp(type, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") == 0
				|| strcasecmp(type, "EFI") == 0
				|| strcmp(type, "21686148-6449-6e6f-744e-656564454649") == 0)
				return false;
		}
		if (!_IsSupportedTargetFs(partition->ContentType()))
			return false;
		if (partition->ContentSize() < (1LL << 30))
			return false;
		BString contentName = partition->ContentName();
		BString label;
		if (!contentName.IsEmpty())
			label.SetToFormat("%s (%s)", contentName.String(), path.Path());
		else
			label.SetTo(path.Path());
		PartitionMenuItem* item = new PartitionMenuItem(
			contentName.String(), label.String(), NULL,
			new BMessage(TARGET_PARTITION), partition->ID());
		fMenu->AddItem(item);
		return false;
	}

private:
	BMenu* fMenu;
};


// Collects EFI System Partitions for the "Install bootloader" submenu,
// using the same predicate TargetVisitor uses to exclude ESPs from targets.
class EFIVisitor : public BDiskDeviceVisitor {
public:
	EFIVisitor(BMenu* menu)
		:
		fMenu(menu)
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
		const char* type = partition->Type();
		if (type == NULL)
			return false;
		if (strcmp(type, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") != 0
			&& strcasecmp(type, "EFI") != 0)
			return false;
		BPath path;
		if (partition->GetPath(&path) != B_OK)
			return false;
		BString contentName = partition->ContentName();
		BString label;
		if (!contentName.IsEmpty())
			label.SetToFormat("%s (%s)", contentName.String(), path.Path());
		else
			label.SetTo(path.Path());
		BMessage* msg = new BMessage(MSG_INSTALL_BOOTLOADER);
		msg->AddInt64("id", partition->ID());
		PartitionMenuItem* item = new PartitionMenuItem(
			contentName.String(), label.String(), NULL, msg,
			partition->ID());
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
	fInPlace(false),
	fLastReportedPercent(0)
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
				int64 rawTarget = -1;
				message->FindInt64("target", &rawTarget);
				_PerformInstall((partition_id)rawTarget);
			}
			break;
		}
		case MSG_INSTALL_BOOTLOADER:
		{
			int64 id = -1;
			message->FindInt64("id", &id);
			_DoInstallBootloader((partition_id)id);
			break;
		}
		case MSG_BOOTLOADER_SETUP:
			_DoBootloaderSetup();
			break;
		default:
			BLooper::MessageReceived(message);
	}
}


void
WorkerThread::ScanDisksPartitions(BMenu* dstMenu, BMenu* efiMenu)
{
	fDDRoster.RewindDevices();
	BDiskDevice device;
	while (fDDRoster.GetNextDevice(&device) == B_OK) {
		TargetVisitor visitor(dstMenu);
		device.VisitEachDescendant(&visitor);
		if (efiMenu != NULL) {
			EFIVisitor efiVisitor(efiMenu);
			device.VisitEachDescendant(&efiVisitor);
		}
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
	msg.AddInt64("target", targetPartitionID);
	PostMessage(&msg);
}


void
WorkerThread::InstallBootloader(partition_id id)
{
	BMessage msg(MSG_INSTALL_BOOTLOADER);
	msg.AddInt64("id", id);
	PostMessage(&msg);
}


void
WorkerThread::_DoInstallBootloader(partition_id id)
{
	BDiskDevice device;
	BPartition* partition;
	status_t err = fDDRoster.GetPartitionWithID(id, &device, &partition);
	if (err != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
			B_TRANSLATE("The selected EFI System Partition could not "
				"be found."), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	BPath devicePath;
	if (partition->GetPath(&devicePath) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
			B_TRANSLATE("The selected EFI System Partition path could "
				"not be resolved."), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	int outPipe[2];
	if (pipe2(outPipe, O_CLOEXEC) < 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
			B_TRANSLATE("Could not start the installer helper."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->Go();
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
			B_TRANSLATE("Could not start the installer helper."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->Go();
		return;
	}
	if (pid == 0) {
		dup2(outPipe[1], STDOUT_FILENO);
		dup2(outPipe[1], STDERR_FILENO);
		close(outPipe[0]);
		close(outPipe[1]);
		execl("/usr/bin/pkexec", "pkexec", kInstallHelper, "bootloader",
			devicePath.Path(), (char*)NULL);
		_exit(127);
	}
	close(outPipe[1]);

	BString lastOutput;
	const size_t kMaxLastOutput = 4096;
	char readBuf[4096];
	for (;;) {
		ssize_t n = read(outPipe[0], readBuf, sizeof(readBuf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		lastOutput.Append(readBuf, n);
		if (lastOutput.Length() > (int32)kMaxLastOutput) {
			lastOutput.Remove(0, lastOutput.Length() - kMaxLastOutput);
		}
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
			B_TRANSLATE("Bootloader installed successfully."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_INFO_ALERT);
		alert->Go();
		return;
	}

	int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	BString text;
	text.SetToFormat(B_TRANSLATE("Bootloader installation failed "
		"(helper exit code %d):\n%s"), code, lastOutput.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Install bootloader"),
		text.String(), B_TRANSLATE("OK"), NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


void
WorkerThread::BootloaderSetup()
{
	PostMessage(MSG_BOOTLOADER_SETUP);
}


void
WorkerThread::_DoBootloaderSetup()
{
	int outPipe[2];
	if (pipe2(outPipe, O_CLOEXEC) < 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Bootloader setup"),
			B_TRANSLATE("Could not start the installer helper."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->Go();
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		BAlert* alert = new BAlert(B_TRANSLATE("Bootloader setup"),
			B_TRANSLATE("Could not start the installer helper."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_STOP_ALERT);
		alert->Go();
		return;
	}
	if (pid == 0) {
		dup2(outPipe[1], STDOUT_FILENO);
		dup2(outPipe[1], STDERR_FILENO);
		close(outPipe[0]);
		close(outPipe[1]);
		execl("/usr/bin/pkexec", "pkexec", kInstallHelper, "bootloader",
			"--setup", (char*)NULL);
		_exit(127);
	}
	close(outPipe[1]);

	BString output;
	const size_t kMaxOutput = 8192;
	char readBuf[4096];
	for (;;) {
		ssize_t n = read(outPipe[0], readBuf, sizeof(readBuf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		output.Append(readBuf, n);
		if (output.Length() > (int32)kMaxOutput)
			output.Remove(0, output.Length() - kMaxOutput);
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		BString text(output);
		if (text.Length() == 0)
			text = B_TRANSLATE("No GRUB menu entries found.");
		BAlert* alert = new BAlert(B_TRANSLATE("Bootloader setup"),
			text.String(), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->Go();
		return;
	}

	int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	BString text;
	text.SetToFormat(B_TRANSLATE("Bootloader setup failed "
		"(helper exit code %d):\n%s"), code, output.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Bootloader setup"),
		text.String(), B_TRANSLATE("OK"), NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
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

	BPath devicePath;
	if (partition->GetPath(&devicePath) != B_OK)
		return _InstallationError(B_ERROR);

	_SetStatusMessage(B_TRANSLATE("Installing..."));
	BString detail;
	err = _RunFullInstall(devicePath.Path(), &detail);
	if (err != B_OK)
		return _InstallationError(err, detail);

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
WorkerThread::_RunFullInstall(const char* devicePath, BString* detail)
{
	if (fSetupConf.Length() == 0)
		return B_BAD_VALUE;

	int inPipe[2];
	int outPipe[2];
	if (pipe2(inPipe, O_CLOEXEC) < 0)
		return B_ERROR;
	if (pipe2(outPipe, O_CLOEXEC) < 0) {
		close(inPipe[0]);
		close(inPipe[1]);
		return B_ERROR;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);
		return B_ERROR;
	}
	if (pid == 0) {
		dup2(inPipe[0], STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		dup2(outPipe[1], STDERR_FILENO);
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);
		execl("/usr/bin/pkexec", "pkexec", kInstallHelper, "--full-install",
			devicePath, (char*)NULL);
		_exit(127);
	}

	close(inPipe[0]);
	close(outPipe[1]);

	const char* buf = fSetupConf.String();
	size_t left = fSetupConf.Length();
	status_t writeErr = B_OK;
	while (left > 0) {
		ssize_t n = write(inPipe[1], buf, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			writeErr = B_ERROR;
			break;
		}
		buf += n;
		left -= n;
	}
	close(inPipe[1]);

	explicit_bzero(fSetupConf.LockBuffer(0), fSetupConf.Length());
	fSetupConf.UnlockBuffer(0);
	fSetupConf.Truncate(0);

	ProgressReporter reporter(fOwner, new BMessage(MSG_STATUS_MESSAGE));
	reporter.Reset();
	reporter.AddItems(1, 100);
	reporter.StartTimer();
	fLastReportedPercent = 0;

	char readBuf[4096];
	BString line;
	status_t readErr = B_OK;
	bool canceled = false;
	BString lastOutput;
	const size_t kMaxLastOutput = 4096;
	for (;;) {
		if (fCancelSemaphore >= 0
				&& acquire_sem_etc(fCancelSemaphore, 1, B_RELATIVE_TIMEOUT, 0)
					== B_OK) {
			canceled = true;
			kill(pid, SIGTERM);
			break;
		}

		struct pollfd pfd = { outPipe[0], POLLIN, 0 };
		int pr = poll(&pfd, 1, kCancelPollMs);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			readErr = B_ERROR;
			break;
		}
		if (pr == 0)
			continue;

		ssize_t n = read(outPipe[0], readBuf, sizeof(readBuf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			readErr = B_ERROR;
			break;
		}
		if (n == 0)
			break;

		for (ssize_t i = 0; i < n; i++) {
			char c = readBuf[i];
			if (c == '\n' || c == '\r') {
				if (line.Length() > 0) {
					if (!_HandleProgressLine(line.String(), &reporter)) {
						lastOutput << line << "\n";
						if (lastOutput.Length() > (int32)kMaxLastOutput) {
							lastOutput.Remove(0,
								lastOutput.Length() - kMaxLastOutput);
						}
					}
					line.Truncate(0);
				}
			} else {
				line += c;
			}
		}
	}

	if (canceled) {
		while (read(outPipe[0], readBuf, sizeof(readBuf)) > 0)
			;
	}
	close(outPipe[0]);

	int status = 0;
	bigtime_t deadline = canceled
		? system_time() + kCancelKillDeadlineUsec : 0;
	while (true) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			break;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
		if (deadline > 0 && system_time() >= deadline) {
			kill(pid, SIGKILL);
			deadline = 0;
			continue;
		}
		struct timespec ts = { 0, kWaitpidPollNsec };
		nanosleep(&ts, NULL);
	}

	if (canceled)
		return B_CANCELED;
	if (writeErr != B_OK || readErr != B_OK) {
		if (detail != NULL)
			*detail = lastOutput;
		return B_ERROR;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return B_OK;

	fprintf(stderr, "WorkerThread: vos-install-helper exit=%d\n", status);
	if (detail != NULL) {
		if (WIFEXITED(status)) {
			BString exitLine;
			exitLine.SetToFormat("vos-install-helper exited with code %d\n",
				WEXITSTATUS(status));
			lastOutput.Prepend(exitLine);
		} else if (WIFSIGNALED(status)) {
			BString sigLine;
			sigLine.SetToFormat(
				"vos-install-helper killed by signal %d\n", WTERMSIG(status));
			lastOutput.Prepend(sigLine);
		}
		*detail = lastOutput;
	}
	return B_ERROR;
}


bool
WorkerThread::_HandleProgressLine(const char* line, ProgressReporter* reporter)
{
	const char* p = line;
	while (*p == ' ' || *p == '\t')
		p++;
	while (*p && *p != ' ' && *p != '\t')
		p++;
	while (*p == ' ' || *p == '\t')
		p++;

	char* endptr = NULL;
	long pct = strtol(p, &endptr, 10);
	if (endptr == p || pct < 0 || pct > 100 || *endptr != '%')
		return false;

	if (reporter != NULL) {
		int delta = (int)pct - fLastReportedPercent;
		if (delta > 0) {
			reporter->ItemsWritten(0, delta, NULL, NULL);
			fLastReportedPercent = pct;
		}
	}
	return true;
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
WorkerThread::_InstallationError(status_t error, const BString& detail)
{
	BMessage msg(MSG_RESET);
	msg.AddInt32("error", error);
	if (!detail.IsEmpty())
		msg.AddString("detail", detail);
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
