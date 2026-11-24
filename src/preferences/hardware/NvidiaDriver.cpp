/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "NvidiaDriver.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Entry.h>
#include <FindDirectory.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>
#include <String.h>

#include <GLRendererSelect.h>


extern char** environ;

static const char* kHelper = "/usr/libexec/vos-nvidia-enable";
static const char* kDecide = "/usr/libexec/nvidia-decide.sh";
static const char* kEnabledMarker = "/etc/vos/nvidia.conf";
static const size_t kMaxOutput = 4096;


struct NvidiaJob {
	BMessenger	target;
	bool		enable;
	bool		acceptRealtime;
};


bool
NvidiaDriverEnabled()
{
	return access(kEnabledMarker, F_OK) == 0;
}


bool
NvidiaDriverAvailable()
{
	if (NvidiaDriverEnabled())
		return true;
	if (access(kDecide, X_OK) != 0)
		return false;

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
		O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
		O_WRONLY, 0);

	char* argv[] = { (char*)kDecide, (char*)"check", NULL };
	pid_t pid = -1;
	int status = -1;
	if (posix_spawn(&pid, kDecide, &actions, NULL, argv, environ) == 0) {
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}
	posix_spawn_file_actions_destroy(&actions);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


bool
NvidiaKernelIsRealtime()
{
	FILE* file = fopen("/sys/kernel/realtime", "r");
	if (file != NULL) {
		int value = 0;
		bool realtime = fscanf(file, "%d", &value) == 1 && value == 1;
		fclose(file);
		if (realtime)
			return true;
	}

	struct utsname name;
	return uname(&name) == 0 && strstr(name.release, "-rt") != NULL;
}


static int32
_ApplyThread(void* data)
{
	NvidiaJob* job = (NvidiaJob*)data;

	int outputPipe[2] = { -1, -1 };
	bool piped = pipe(outputPipe) == 0;

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
		O_RDONLY, 0);
	if (piped) {
		posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
		posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
	}

	// posix_spawn, not fork(): no fork handlers run in this
	// multithreaded process for a child that only execs.
	const char* flag = NULL;
	if (!job->enable)
		flag = "--disable";
	else if (job->acceptRealtime)
		flag = "--accept-rt";
	char* argv[] = { (char*)"pkexec", (char*)kHelper, (char*)flag, NULL };

	int status = -1;
	pid_t pid = -1;
	BString output;
	if (posix_spawnp(&pid, "pkexec", &actions, NULL, argv, environ) == 0) {
		if (piped) {
			close(outputPipe[1]);
			outputPipe[1] = -1;
			char buffer[512];
			ssize_t bytes;
			while ((bytes = read(outputPipe[0], buffer, sizeof(buffer)))
					> 0) {
				output.Append(buffer, bytes);
				if ((size_t)output.Length() > 2 * kMaxOutput) {
					output.Remove(0, output.Length() - kMaxOutput);
				}
			}
		}
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}
	posix_spawn_file_actions_destroy(&actions);
	if (piped) {
		close(outputPipe[0]);
		if (outputPipe[1] >= 0)
			close(outputPipe[1]);
	}

	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	// pkexec exits 126 when the prompt is dismissed and 127 when not authorized.
	bool cancelled = WIFEXITED(status)
		&& (WEXITSTATUS(status) == 126 || WEXITSTATUS(status) == 127);

	if ((size_t)output.Length() > kMaxOutput)
		output.Remove(0, output.Length() - kMaxOutput);

	BMessage result(kMsgNvidiaDriverResult);
	result.AddBool("ok", ok);
	result.AddBool("cancelled", cancelled);
	result.AddString("text", output.String());
	job->target.SendMessage(&result);

	delete job;
	return B_OK;
}


status_t
NvidiaDriverApplyAsync(bool enable, bool acceptRealtime,
	const BMessenger& replyTo)
{
	if (!replyTo.IsValid())
		return B_BAD_VALUE;

	NvidiaJob* job = new NvidiaJob;
	job->target = replyTo;
	job->enable = enable;
	job->acceptRealtime = acceptRealtime;

	thread_id thread = spawn_thread(_ApplyThread, "hardware:nvidia",
		B_NORMAL_PRIORITY, job);
	if (thread < 0) {
		delete job;
		return thread;
	}

	resume_thread(thread);
	return B_OK;
}


void
NvidiaDriverForgetRendererSelection()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
			|| path.Append(GLRendererSelect::kSelectionPath) != B_OK) {
		return;
	}
	BEntry(path.Path()).Remove();
}
