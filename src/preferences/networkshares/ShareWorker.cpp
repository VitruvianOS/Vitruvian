/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ShareWorker.h"

#include <Messenger.h>
#include <String.h>

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

#include "NetworkSharesWindow.h"


extern char** environ;


static const char* kHelper = "/usr/libexec/vos-network-shares-helper";


class HelperJob {
public:
					HelperJob(const BString& command, const BString& args,
						const BString& stdinData, const BMessenger& reply)
			:
			fCommand(command),
			fArgs(args),
			fStdinData(stdinData),
			fReply(reply)
	{
	}

	BString		fCommand;
	BString		fArgs;
	BString		fStdinData;
	BMessenger	fReply;
};


static status_t
_HelperThread(void* data)
{
	HelperJob* job = (HelperJob*)data;

	std::vector<BString*> tokens;
	tokens.push_back(new BString(kHelper));
	tokens.push_back(new BString(job->fCommand));

	const char* text = job->fArgs.String();
	const char* p = text;
	while (*p != '\0') {
		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		const char* start = p;
		while (*p != '\0' && *p != ' ')
			p++;
		tokens.push_back(new BString(start, (int32)(p - start)));
	}

	std::vector<char*> cargv;
	for (size_t i = 0; i < tokens.size(); i++)
		cargv.push_back(const_cast<char*>(tokens[i]->String()));
	cargv.push_back(NULL);

	int stdinPipe[2] = { -1, -1 };
	int closeStdinRead = 0;
	if (!job->fStdinData.IsEmpty()) {
		if (pipe(stdinPipe) != 0) {
			stdinPipe[0] = stdinPipe[1] = -1;
		} else {
			closeStdinRead = 1;
		}
	}

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	if (stdinPipe[1] >= 0)
		posix_spawn_file_actions_adddup2(&actions, stdinPipe[1], 0);

	pid_t pid = -1;
	int err = posix_spawnp(&pid, "pkexec", &actions, NULL, &cargv[0], environ);
	posix_spawn_file_actions_destroy(&actions);

	if (stdinPipe[1] >= 0)
		close(stdinPipe[1]);

	if (err == 0 && stdinPipe[0] >= 0) {
		const char* data = job->fStdinData.String();
		size_t len = job->fStdinData.Length();
		while (len > 0) {
			ssize_t wrote = write(stdinPipe[0], data, len);
			if (wrote < 0 && errno == EINTR)
				continue;
			if (wrote <= 0)
				break;
			data += wrote;
			len -= (size_t)wrote;
		}
	}
	if (stdinPipe[0] >= 0)
		close(stdinPipe[0]);
	(void)closeStdinRead;

	int helperStatus = B_OK;
	if (err == 0) {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			helperStatus = B_ERROR;
	} else {
		fprintf(stderr, "Network shares: pkexec failed: %s\n", strerror(err));
		helperStatus = -err;
	}

	BMessage done(kMsgHelperDone);
	done.AddInt32("status", helperStatus);
	job->fReply.SendMessage(&done);

	for (size_t i = 0; i < tokens.size(); i++)
		delete tokens[i];
	delete job;
	return B_OK;
}


/*static*/ bool
ShareWorker::Post(const char* command, const BString& args,
	const BMessenger& reply, const BString& stdinData)
{
	HelperJob* job = new HelperJob(command, args, stdinData, reply);
	thread_id thread = spawn_thread(_HelperThread, "network shares helper",
		B_NORMAL_PRIORITY, job);
	if (thread < B_OK) {
		delete job;
		return false;
	}
	return resume_thread(thread) == B_OK;
}
