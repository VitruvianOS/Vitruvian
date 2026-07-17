/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */
#include "CopyEngine.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <OS.h>
#include <String.h>

#include "ProgressReporter.h"


static const char* kExcludesPath = "/usr/share/vos/installer/excludes.list";

static const bigtime_t kCancelKillDeadlineUsec = 5 * 1000000LL;
static const long      kWaitpidPollNsec        = 50 * 1000000L;
static const int       kCancelPollMs           = 200;


CopyEngine::CopyEngine(ProgressReporter* reporter, sem_id cancelSem)
	:
	fReporter(reporter),
	fCancelSem(cancelSem),
	fLastReportedPercent(0)
{
}


CopyEngine::~CopyEngine()
{
}


status_t
CopyEngine::Copy(const char* source, const char* target)
{
	if (source == NULL || *source == '\0'
			|| target == NULL || *target == '\0')
		return B_BAD_VALUE;

	if (fReporter != NULL) {
		fReporter->Reset();
		fReporter->AddItems(1, 100);
		fReporter->StartTimer();
	}
	fLastReportedPercent = 0;

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
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[0]);
		close(pipefd[1]);

		char srcArg[PATH_MAX];
		char dstArg[PATH_MAX];
		snprintf(srcArg, sizeof(srcArg), "%s/", source);
		snprintf(dstArg, sizeof(dstArg), "%s/", target);

		execlp("rsync", "rsync",
			"-aHAXx",
			"--numeric-ids",
			"--info=progress2",
			"--exclude-from", kExcludesPath,
			srcArg, dstArg,
			(char*)NULL);
		_exit(127);
	}

	close(pipefd[1]);
	status_t st = _RunRsync(source, target, pipefd[0]);
	close(pipefd[0]);

	if (st == B_CANCELED)
		kill(pid, SIGTERM);

	// SIGKILL fallback if rsync ignores SIGTERM after cancel.
	int status = 0;
	bigtime_t deadline = (st == B_CANCELED)
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

	if (st != B_OK)
		return st;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return B_OK;

	fprintf(stderr, "CopyEngine: rsync failed status=%d\n", status);
	return B_ERROR;
}


status_t
CopyEngine::_RunRsync(const char* /*source*/, const char* /*target*/, int outFd)
{
	char buf[4096];
	BString line;

	for (;;) {
		if (fCancelSem >= 0
				&& acquire_sem_etc(fCancelSem, 1, B_RELATIVE_TIMEOUT, 0)
					== B_OK) {
			return B_CANCELED;
		}

		struct pollfd pfd = { outFd, POLLIN, 0 };
		int pr = poll(&pfd, 1, kCancelPollMs);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
		if (pr == 0)
			continue;

		ssize_t n = read(outFd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
		if (n == 0)
			break;

		for (ssize_t i = 0; i < n; i++) {
			char c = buf[i];
			if (c == '\n' || c == '\r') {
				if (line.Length() > 0) {
					_HandleProgressLine(line.String());
					line.Truncate(0);
				}
			} else {
				line += c;
			}
		}
	}

	if (line.Length() > 0)
		_HandleProgressLine(line.String());

	return B_OK;
}


void
CopyEngine::_HandleProgressLine(const char* line)
{
	// progress2: "  123,456,789  42%  12.34MB/s  ..."
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
		return;

	if (fReporter == NULL)
		return;
	int delta = (int)pct - fLastReportedPercent;
	if (delta <= 0)
		return;
	fReporter->ItemsWritten(0, delta, NULL, NULL);
	fLastReportedPercent = pct;
}
