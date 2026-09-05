/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AptBackend.h"

#include <Autolock.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static const char* kAptHelper = "/usr/libexec/vos-apt-helper";
static const char* kPkexec = "/usr/bin/pkexec";

static const char* const kLockPaths[] = {
	"/var/lib/dpkg/lock-frontend",
	"/var/lib/dpkg/lock",
	"/var/lib/apt/lists/lock",
	"/var/cache/apt/archives/lock"
};


AptBackend::AptBackend()
	:
	fLastError(kAptErrorNone),
	fLockHolderPid(-1),
	fCancelRequested(false),
	fProgressHook(NULL),
	fProgressCookie(NULL)
{
}


AptBackend::~AptBackend()
{
}


static void
append_names(const BObjectList<BString>& names, const char** argv, int& at)
{
	for (int32 i = 0; i < names.CountItems(); i++)
		argv[at++] = names.ItemAt(i)->String();
}


status_t
AptBackend::_RunVerb(const char* verb, const BObjectList<BString>& names)
{
	const int32 count = names.CountItems();

	// Mirrors the bound the helper itself enforces as root.
	if (count > kMaxTransactionBatchSize) {
		BString detail;
		detail.SetToFormat("too many packages for '%s' (%" B_PRId32
			" > %" B_PRId32 ")", verb, count, kMaxTransactionBatchSize);
		_SetError(kAptErrorBatchTooLarge, detail.String());
		return B_ERROR;
	}

	const char** argv = new const char*[count + 4];
	int at = 0;
	argv[at++] = kPkexec;
	argv[at++] = kAptHelper;
	argv[at++] = verb;
	append_names(names, argv, at);
	argv[at] = NULL;

	status_t result = _RunHelper(argv);
	delete[] argv;
	return result;
}


status_t
AptBackend::SimulateTransaction(const BObjectList<BString>& install,
	const BObjectList<BString>& remove, const BObjectList<BString>& purge,
	BString* summary)
{
	if (summary == NULL)
		return B_BAD_VALUE;

	summary->SetTo("");

	struct SimGroup { const char* verb; const BObjectList<BString>* names; };
	SimGroup groups[] = {
		{ "install", &install },
		{ "remove", &remove },
		{ "purge", &purge }
	};

	for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
		if (groups[g].names->IsEmpty())
			continue;

		const int32 count = groups[g].names->CountItems();

		// Mirrors the bound the helper itself enforces.
		if (count > kMaxTransactionBatchSize) {
			BString detail;
			detail.SetToFormat("too many packages for '%s' (%" B_PRId32
				" > %" B_PRId32 ")", groups[g].verb, count,
				kMaxTransactionBatchSize);
			_SetError(kAptErrorBatchTooLarge, detail.String());
			return B_ERROR;
		}

		const char** argv = new const char*[count + 5];
		int at = 0;
		argv[at++] = "/usr/bin/apt-get";
		argv[at++] = "-s";
		argv[at++] = groups[g].verb;
		argv[at++] = "--";
		for (int32 i = 0; i < count; i++)
			argv[at++] = groups[g].names->ItemAt(i)->String();
		argv[at] = NULL;

		BObjectList<BString, true> lines(64);
		status_t result = _RunQuery(argv, &lines);
		delete[] argv;

		if (result != B_OK) {
			_SetError(kAptErrorBrokenDeps,
				"apt could not resolve these changes");
			return B_ERROR;
		}

		for (int32 i = 0; i < lines.CountItems(); i++) {
			const BString& line = *lines.ItemAt(i);
			if (line.Length() == 0)
				continue;
			if (line.StartsWith("Inst ") || line.StartsWith("Conf ")
				|| line.StartsWith("NOTE:") || line.StartsWith("      ")
				|| line.StartsWith("Reading ")
				|| line.StartsWith("Building ")) {
				continue;
			}
			*summary << line << "\n";
		}
	}

	return B_OK;
}


status_t
AptBackend::ApplyTransaction(const BObjectList<BString>& install,
	const BObjectList<BString>& remove, const BObjectList<BString>& purge)
{
	if (install.IsEmpty() && remove.IsEmpty() && purge.IsEmpty())
		return B_OK;

	_ResetCancel();

	if (!_CheckDpkgLock())
		return B_ERROR;

	// Cancellation only takes effect between verb groups, never mid-run.
	if (!install.IsEmpty()) {
		if (_CancelRequested()) {
			_SetError(kAptErrorInterrupted, "cancelled before starting");
			return B_ERROR;
		}
		if (_RunVerb("install", install) != B_OK)
			return B_ERROR;
	}
	if (!remove.IsEmpty()) {
		if (_CancelRequested()) {
			_SetError(kAptErrorInterrupted, "cancelled before starting");
			return B_ERROR;
		}
		if (_RunVerb("remove", remove) != B_OK)
			return B_ERROR;
	}
	if (!purge.IsEmpty()) {
		if (_CancelRequested()) {
			_SetError(kAptErrorInterrupted, "cancelled before starting");
			return B_ERROR;
		}
		if (_RunVerb("purge", purge) != B_OK)
			return B_ERROR;
	}

	return B_OK;
}


status_t
AptBackend::ConfigurePending()
{
	if (!_CheckDpkgLock())
		return B_ERROR;

	BObjectList<BString> none;
	return _RunVerb("configure-pending", none);
}


status_t
AptBackend::Update()
{
	if (!_CheckDpkgLock())
		return B_ERROR;

	BObjectList<BString> none;
	return _RunVerb("update", none);
}


status_t
AptBackend::Upgrade(bool full)
{
	if (!_CheckDpkgLock())
		return B_ERROR;

	BObjectList<BString> none;
	return _RunVerb(full ? "full-upgrade" : "upgrade", none);
}


status_t
AptBackend::SetChannel(package_channel channel)
{
	// TODO: rewrite the sources entry and keyring for the target channel.
	return B_UNSUPPORTED;
}


bool
AptBackend::_ReadFile(const char* path, BString* out)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	char buffer[4096];
	size_t bytesRead;
	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
		out->Append(buffer, bytesRead);
	fclose(file);
	return true;
}


void
AptBackend::RequestCancel()
{
	BAutolock lock(fCancelLock);
	fCancelRequested = true;
}


bool
AptBackend::_CancelRequested()
{
	BAutolock lock(fCancelLock);
	return fCancelRequested;
}


void
AptBackend::_ResetCancel()
{
	BAutolock lock(fCancelLock);
	fCancelRequested = false;
}


void
AptBackend::SetProgressHook(progress_hook hook, void* cookie)
{
	fProgressHook = hook;
	fProgressCookie = cookie;
}


static void
collect_line(const BString& line, void* cookie)
{
	((BObjectList<BString, true>*)cookie)->AddItem(new BString(line));
}


status_t
AptBackend::_RunQuery(const char* const argv[],
	BObjectList<BString, true>* lines)
{
	if (lines == NULL)
		return B_BAD_VALUE;

	return _RunQueryStreaming(argv, collect_line, lines);
}


status_t
AptBackend::_RunQueryStreaming(const char* const argv[],
	void (*handler)(const BString& line, void* cookie), void* cookie)
{
	if (argv == NULL || argv[0] == NULL || handler == NULL)
		return B_BAD_VALUE;

	int pipeFds[2];
	if (pipe(pipeFds) != 0)
		return B_ERROR;

	pid_t child = fork();
	if (child < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return B_ERROR;
	}

	if (child == 0) {
		static char* const kChildEnv[] = {
			(char*)"LANG=C",
			(char*)"LC_ALL=C",
			(char*)"DEBIAN_FRONTEND=noninteractive",
			(char*)"PATH=/usr/bin:/bin",
			NULL
		};
		close(pipeFds[0]);
		if (dup2(pipeFds[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(pipeFds[1]);
		int devNull = open("/dev/null", O_WRONLY);
		if (devNull >= 0) {
			dup2(devNull, STDERR_FILENO);
			close(devNull);
		}
		execve(argv[0], (char* const*)argv, kChildEnv);
		_exit(127);
	}

	close(pipeFds[1]);

	BString pending;
	char buffer[4096];
	ssize_t bytesRead;
	while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
		int32 start = 0;
		for (ssize_t i = 0; i < bytesRead; i++) {
			if (buffer[i] != '\n')
				continue;
			pending.Append(buffer + start, i - start);
			handler(pending, cookie);
			pending.SetTo("");
			start = i + 1;
		}
		if (start < bytesRead)
			pending.Append(buffer + start, bytesRead - start);
	}
	if (pending.Length() > 0)
		handler(pending, cookie);

	close(pipeFds[0]);

	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		;

	if (bytesRead < 0)
		return B_ERROR;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return B_ERROR;
	return B_OK;
}


status_t
AptBackend::_RunHelper(const char* const argv[])
{
	if (argv == NULL || argv[0] == NULL)
		return B_BAD_VALUE;

	int pipeFds[2];
	if (pipe(pipeFds) != 0)
		return B_ERROR;

	pid_t child = fork();
	if (child < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return B_ERROR;
	}

	if (child == 0) {
		static char* const kChildEnv[] = {
			(char*)"LANG=C",
			(char*)"LC_ALL=C",
			(char*)"DEBIAN_FRONTEND=noninteractive",
			(char*)"PATH=/usr/bin:/bin",
			NULL
		};
		close(pipeFds[0]);
		if (dup2(pipeFds[1], STDOUT_FILENO) < 0)
			_exit(127);
		if (dup2(pipeFds[1], STDERR_FILENO) < 0)
			_exit(127);
		close(pipeFds[1]);
		execve(argv[0], (char* const*)argv, kChildEnv);
		_exit(127);
	}

	close(pipeFds[1]);

	BString output;
	BString pending;
	char buffer[4096];
	ssize_t bytesRead;
	while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
		int32 start = 0;
		for (ssize_t i = 0; i < bytesRead; i++) {
			if (buffer[i] != '\n')
				continue;
			pending.Append(buffer + start, i - start);
			_ReportProgress(pending);
			output << pending << "\n";
			pending.SetTo("");
			start = i + 1;
		}
		if (start < bytesRead)
			pending.Append(buffer + start, bytesRead - start);
	}
	if (pending.Length() > 0) {
		_ReportProgress(pending);
		output << pending;
	}

	close(pipeFds[0]);

	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		;

	if (!WIFEXITED(status)) {
		_SetError(kAptErrorInterrupted, "helper terminated abnormally");
		return B_ERROR;
	}

	int exitStatus = WEXITSTATUS(status);
	if (exitStatus == 0)
		return B_OK;

	_SetError(_ClassifyOutput(output, exitStatus), output.String());
	return B_ERROR;
}


void
AptBackend::_ReportProgress(const BString& line)
{
	if (fProgressHook == NULL)
		return;

	// dlstatus's middle field is a download item number, not a package name.
	bool isPackageStatus = line.StartsWith("pmstatus:");
	if (isPackageStatus || line.StartsWith("dlstatus:")) {
		int32 first = line.FindFirst(':');
		int32 second = line.FindFirst(':', first + 1);
		int32 third = line.FindFirst(':', second + 1);
		if (second > 0 && third > second) {
			BString name;
			if (isPackageStatus)
				line.CopyInto(name, first + 1, second - first - 1);
			BString percentText;
			line.CopyInto(percentText, second + 1, third - second - 1);
			BString text;
			line.CopyInto(text, third + 1, line.Length() - third - 1);
			fProgressHook((int32)atof(percentText.String()), text.String(),
				name.String(), fProgressCookie);
			return;
		}
	}

	fProgressHook(-1, line.String(), "", fProgressCookie);
}


bool
AptBackend::_CheckDpkgLock()
{
	fLockHolderPid = -1;
	fLockPath.SetTo("");

	// Lock files are root:root 0640; open() can't test them, so match
	// against /proc/locks by device/inode instead.
	BString locks;
	if (!_ReadFile("/proc/locks", &locks))
		return true;

	for (size_t i = 0; i < sizeof(kLockPaths) / sizeof(kLockPaths[0]); i++) {
		struct stat st;
		if (stat(kLockPaths[i], &st) != 0)
			continue;

		BString key;
		key.SetToFormat("%02x:%02x:%llu", major(st.st_dev), minor(st.st_dev),
			(unsigned long long)st.st_ino);

		int32 at = locks.FindFirst(key.String());
		if (at < 0)
			continue;

		int32 lineStart = locks.FindLast("\n", at);
		lineStart = (lineStart < 0) ? 0 : lineStart + 1;
		int32 lineEnd = locks.FindFirst("\n", at);
		if (lineEnd < 0)
			lineEnd = locks.Length();
		BString line;
		locks.CopyInto(line, lineStart, lineEnd - lineStart);

		int id = 0;
		char type[16] = { 0 };
		char mode[16] = { 0 };
		char rw[16] = { 0 };
		int pid = -1;
		if (sscanf(line.String(), "%d: %15s %15s %15s %d", &id, type, mode,
				rw, &pid) == 5) {
			fLockHolderPid = (pid_t)pid;
		}
		fLockPath.SetTo(kLockPaths[i]);

		BString detail;
		detail.SetToFormat("%s held by pid %d", kLockPaths[i], pid);
		_SetError(kAptErrorLockHeld, detail.String());
		return false;
	}

	return true;
}


apt_error
AptBackend::_ClassifyOutput(const BString& output, int exitStatus)
{
	// 126/127 are pkexec's own codes, not an apt failure.
	if (exitStatus == 126)
		return kAptErrorNoPrivilege;
	if (exitStatus == 127)
		return kAptErrorNoPrivilege;

	if (exitStatus == 2 && output.IFindFirst("too many packages") >= 0)
		return kAptErrorBatchTooLarge;
	if (exitStatus == 2 && output.IFindFirst("rejected package name") >= 0)
		return kAptErrorUnknown;

	if (output.IFindFirst("Could not get lock") >= 0
		|| output.IFindFirst("Unable to acquire the dpkg frontend lock") >= 0) {
		return kAptErrorLockHeld;
	}
	if (output.IFindFirst("Unable to correct problems") >= 0
		|| output.IFindFirst("broken packages") >= 0
		|| output.IFindFirst("unmet dependencies") >= 0) {
		return kAptErrorBrokenDeps;
	}
	if (output.IFindFirst("No space left on device") >= 0
		|| output.IFindFirst("not enough free space") >= 0) {
		return kAptErrorDiskFull;
	}
	if (output.IFindFirst("Temporary failure resolving") >= 0
		|| output.IFindFirst("Could not resolve") >= 0
		|| output.IFindFirst("Connection failed") >= 0
		|| output.IFindFirst("Failed to fetch") >= 0) {
		return kAptErrorNetwork;
	}
	if (output.IFindFirst("NO_PUBKEY") >= 0
		|| output.IFindFirst("not signed") >= 0
		|| output.IFindFirst("signatures were invalid") >= 0) {
		return kAptErrorSignature;
	}

	return kAptErrorUnknown;
}


void
AptBackend::_SetError(apt_error error, const char* detail)
{
	fLastError = error;
	fLastErrorDetail = detail;
}
