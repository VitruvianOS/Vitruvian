/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareHelperClient.h"

#include <errno.h>
#include <OS.h>
#include <String.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "HardwareMessages.h"


// Mirrors the drivesetup run_helper_capture pattern: fork, capture
// stdout, wait for exit 0.
static const char* kPkexecPath = "/usr/bin/pkexec";
static const char* kHelperPath = "/usr/libexec/vos-hardware-helper";


static status_t
_run_helper_capture(const char* execPath, const char* const argv[],
	BString& outText)
{
	int outPipe[2];
	if (pipe(outPipe) < 0)
		return B_ERROR;

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		return B_ERROR;
	}

	if (pid == 0) {
		close(outPipe[0]);
		dup2(outPipe[1], STDOUT_FILENO);
		close(outPipe[1]);
		execv(execPath, (char* const*)argv);
		_exit(127);
	}

	close(outPipe[1]);

	outText.Truncate(0);
	char buffer[512];
	ssize_t bytesRead;
	while ((bytesRead = read(outPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytesRead] = '\0';
		outText << buffer;
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (!WIFEXITED(status))
		return B_ERROR;

	// pkexec exits 126/127 when the dialog was dismissed or the
	// helper is missing; any non-zero exit keeps the unprivileged
	// view without an error box.
	return WEXITSTATUS(status) == 0 ? B_OK : B_ERROR;
}


static int32
_HelperThread(void* data)
{
	BMessenger* replyTo = static_cast<BMessenger*>(data);

	BString text;

	const char* argv[] = { kPkexecPath, kHelperPath, NULL };
	bool ok = _run_helper_capture(kPkexecPath, argv, text) == B_OK;

	BMessage result(kMsgHelperResult);
	result.AddBool("ok", ok);
	if (ok && text.Length() > 0)
		result.AddString("text", text.String());

	replyTo->SendMessage(&result);
	delete replyTo;
	return 0;
}


status_t
HardwareHelperRunAsync(const BMessenger& replyTo)
{
	if (!replyTo.IsValid())
		return B_BAD_VALUE;

	BMessenger* target = new BMessenger(replyTo);

	thread_id thread = spawn_thread(_HelperThread, "hardware:helper",
		B_NORMAL_PRIORITY, target);
	if (thread < 0) {
		delete target;
		return thread;
	}

	resume_thread(thread);
	return B_OK;
}
