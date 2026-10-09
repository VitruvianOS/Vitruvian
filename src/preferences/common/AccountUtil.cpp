/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AccountUtil.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>


namespace BPrivate {
namespace Accounts {


static const char* kReservedNames[] = {
	"root", "daemon", "sys", "bin", "vos_login", "vos-live",
	"_vitruvian-login", "nobody", NULL
};


bool
isReservedName(const char* userName)
{
	if (userName == NULL || userName[0] == '\0')
		return true;
	for (int32 i = 0; kReservedNames[i] != NULL; i++) {
		if (strcmp(userName, kReservedNames[i]) == 0)
			return true;
	}
	return false;
}


static bool
_isInteractiveShell(const char* shell)
{
	if (shell == NULL || shell[0] == '\0')
		return false;
	const char* base = strrchr(shell, '/');
	base = base != NULL ? base + 1 : shell;
	if (strcmp(base, "nologin") == 0 || strcmp(base, "false") == 0)
		return false;
	return true;
}


bool
isAdministrator(const char* userName)
{
	if (userName == NULL)
		return false;
	struct group* sudo = getgrnam("sudo");
	if (sudo == NULL)
		return false;

	int32 ngroups = 0;
	if (getgrouplist(userName, 0, NULL, &ngroups) < 0 || ngroups <= 0)
		return false;
	gid_t* groups = (gid_t*)malloc(ngroups * sizeof(gid_t));
	if (groups == NULL)
		return false;
	if (getgrouplist(userName, 0, groups, &ngroups) < 0) {
		free(groups);
		return false;
	}
	bool admin = false;
	for (int32 i = 0; i < ngroups; i++) {
		if (groups[i] == sudo->gr_gid) {
			admin = true;
			break;
		}
	}
	free(groups);
	return admin;
}


static bool
_hasRunningSession(uid_t uid)
{
	// janus and logind both leave a per-uid directory when a session is up.
	BPath path;
	path.SetTo("/run/user");
	path.Append(BString().SetToFormat("%lu", (unsigned long)uid).String());
	if (BEntry(path.Path()).Exists())
		return true;

	// Fall back to a process scan; /run/user can be absent on early boot.
	BDirectory proc("/proc");
	if (proc.InitCheck() != B_OK)
		return false;
	BEntry entry;
	while (proc.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		if (name[0] < '0' || name[0] > '9')
			continue;
		char statusPath[B_PATH_NAME_LENGTH];
		snprintf(statusPath, sizeof(statusPath), "/proc/%s/status", name);
		FILE* f = fopen(statusPath, "r");
		if (f == NULL)
			continue;
		char line[256];
		bool match = false;
		while (fgets(line, sizeof(line), f) != NULL) {
			unsigned long procUid = 0;
			if (sscanf(line, "Uid:\t%lu", &procUid) == 1) {
				match = (procUid == (unsigned long)uid);
				break;
			}
		}
		fclose(f);
		if (match)
			return true;
	}
	return false;
}


status_t
listAccounts(AccountList* outAccounts)
{
	if (outAccounts == NULL)
		return B_BAD_VALUE;

	uid_t selfUid = getuid();
	setpwent();
	while (struct passwd* pw = getpwent()) {
		if (pw->pw_uid < 1000)
			continue;
		if (!_isInteractiveShell(pw->pw_shell))
			continue;
		if (isReservedName(pw->pw_name))
			continue;

		Account* account = new Account;
		account->name = pw->pw_name;
		account->uid = pw->pw_uid;
		account->isAdmin = isAdministrator(pw->pw_name);
		account->isSelf = (pw->pw_uid == selfUid);
		account->hasSession = _hasRunningSession(pw->pw_uid);

		BString gecos(pw->pw_gecos != NULL ? pw->pw_gecos : "");
		int32 comma = gecos.FindFirst(',');
		if (comma > 0)
			gecos.Truncate(comma);
		account->realName = gecos;

		outAccounts->AddItem(account);
	}
	endpwent();
	return B_OK;
}


static status_t
_captureRun(const char* helper, const StringList* args,
	const char* password, BString& error)
{
	error = "";

	int outPipe[2];
	int inPipe[2];
	if (pipe(outPipe) < 0 || pipe(inPipe) < 0) {
		error = "pipe failed";
		return B_ERROR;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		close(inPipe[0]);
		close(inPipe[1]);
		error = "fork failed";
		return B_ERROR;
	}

	if (pid == 0) {
		close(outPipe[0]);
		close(inPipe[1]);
		if (dup2(outPipe[1], 2) < 0)
			_exit(127);
		close(outPipe[1]);
		if (dup2(inPipe[0], 0) < 0)
			_exit(127);
		close(inPipe[0]);

		int argc = 2;
		if (args != NULL)
			argc += args->CountItems();
		char** argv = (char**)calloc(argc + 1, sizeof(char*));
		if (argv == NULL)
			_exit(127);
		int i = 0;
		argv[i++] = (char*)"pkexec";
		argv[i++] = (char*)helper;
		if (args != NULL) {
			for (int32 j = 0; j < args->CountItems(); j++) {
				BString* item = args->ItemAt(j);
				argv[i++] = (char*)item->String();
			}
		}
		argv[i] = NULL;
		execvp("pkexec", argv);
		_exit(127);
	}

	close(outPipe[1]);
	close(inPipe[0]);

	if (password != NULL && password[0] != '\0') {
		BString line(password);
		line << "\n";
		ssize_t written = write(inPipe[1], line.String(), line.Length());
		(void)written;
	}
	close(inPipe[1]);

	BString output;
	char buffer[256];
	ssize_t n;
	while ((n = read(outPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
		buffer[n] = '\0';
		output.Append(buffer);
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	while (output.Length() > 0) {
		const char* last = output.CharAt(output.Length() - 1);
		if (last != NULL && (*last == '\n' || *last == '\r'))
			output.Truncate(output.Length() - 1);
		else
			break;
	}
	error = output;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		if (error.Length() == 0) {
			if (code == 126 || code == 127)
				error = "Authentication was cancelled or failed.";
			else
				error = BString().SetToFormat(
					"Helper failed (exit %d).", code);
		}
		return B_ERROR;
	}
	return B_OK;
}


status_t
runHelper(const char* helper, const StringList* args,
	const char* password, BString& error)
{
	if (helper == NULL)
		return B_BAD_VALUE;
	return _captureRun(helper, args, password, error);
}


status_t
runAdminHelper(const StringList* args, const char* password,
	BString& error)
{
	return runHelper("/usr/libexec/vos-admin-helper", args, password, error);
}


status_t
runUserHelper(const StringList* args, BString& error)
{
	return runHelper("/usr/libexec/vos-user-helper", args, NULL, error);
}


}	// namespace Accounts
}	// namespace BPrivate
