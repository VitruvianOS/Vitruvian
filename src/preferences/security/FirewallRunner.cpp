/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FirewallRunner.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>


static const char* kHelper = "/usr/libexec/vos-firewall-helper";
static const char* kStatusHelper = "/usr/libexec/vos-firewall-status-helper";
static const int32 kMaxParams = 8;


PrivilegedFirewallRunner::~PrivilegedFirewallRunner()
{
}


status_t
PrivilegedFirewallRunner::Run(const FirewallCommand& command,
	BString& output, int32& exitCode)
{
	output = "";
	exitCode = -1;

	if (command.verb.Length() == 0)
		return B_BAD_VALUE;
	if (command.verb.String()[0] == '-')
		return B_BAD_VALUE;
	if (command.params.size() > (size_t)kMaxParams)
		return B_BAD_VALUE;

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
		if (dup2(outPipe[1], 1) < 0 || dup2(outPipe[1], 2) < 0)
			_exit(127);
		close(outPipe[1]);

		size_t argc = 3 + command.params.size();
		char** argv = (char**)calloc(argc + 1, sizeof(char*));
		if (argv == NULL)
			_exit(127);
		size_t i = 0;
		argv[i++] = (char*)"pkexec";
		argv[i++] = (char*)(firewall_verb_is_read_only(command.verb.String())
			? kStatusHelper : kHelper);
		argv[i++] = (char*)command.verb.String();
		for (size_t j = 0; j < command.params.size(); j++)
			argv[i++] = (char*)command.params[j].String();
		argv[i] = NULL;
		execvp("pkexec", argv);
		_exit(127);
	}

	close(outPipe[1]);

	char buffer[512];
	ssize_t n;
	while ((n = read(outPipe[0], buffer, sizeof(buffer))) > 0)
		output.Append(buffer, (int32)n);
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (!WIFEXITED(status))
		return B_ERROR;
	exitCode = WEXITSTATUS(status);
	if (exitCode == 126 || exitCode == 127) {
		// pkexec uses 126 for a dismissed auth prompt and 127 when the
		// helper is missing; both need a readable message.
		if (output.Length() == 0)
			output = "Authentication was cancelled or failed.";
		return B_ERROR;
	}

	return B_OK;
}
