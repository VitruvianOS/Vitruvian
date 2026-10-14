/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// vos-polkit-grant-helper: setuid root companion to vos-polkit-agent.
//
// After the user's password has been proven (re-checked through the
// same PAM stack the authentication agent used), it records a
// time-limited grant that a generated polkit rule turns into YES for
// org.vitruvian.* actions inside that user's login session. The trust
// shape mirrors polkit-agent-helper-1: root via the setuid bit, the
// password arrives on stdin, and no other privileged operation is
// reachable from an unauthenticated caller.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include <systemd/sd-login.h>

#include "grant_store.h"


static const char* kPamStack = "vitruvian-auth";
static const char* kStateDir = "/run/vos/polkit-grant";
static const char* kRulesDir = "/run/polkit-1/rules.d";
static const char* kRulesPath = "/run/polkit-1/rules.d/70-vos-temp-grant.rules";
static const char* kSessionDir = "/run/systemd/sessions";
static const char* kActionsDir = "/usr/share/polkit-1/actions";
static const char* kAdminGroups[] = { "sudo", "wheel" };
static const char* kWatchedEtcFiles[] = {
	"/etc/shadow", "/etc/group", "/etc/gshadow"
};


struct PasswordConversation {
	const char*	password;
};


static void
state_path_for(const char* session, char* out, size_t outSize)
{
	char sane[128];
	size_t j = 0;
	for (size_t i = 0; session[i] != '\0' && j + 1 < sizeof(sane); i++) {
		char c = session[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_';
		sane[j++] = ok ? c : '_';
	}
	sane[j] = '\0';
	snprintf(out, outSize, "%s/%s.state", kStateDir, sane);
}


static bool
read_whole_file(const char* path, char* out, size_t outSize)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;

	size_t used = 0;
	for (;;) {
		ssize_t n = read(fd, out + used, outSize - used - 1);
		if (n <= 0)
			break;
		used += (size_t)n;
		if (used + 1 >= outSize)
			break;
	}
	close(fd);
	out[used] = '\0';
	return used > 0;
}


static int
pam_conversation(int numMsg, const struct pam_message** message,
	struct pam_response** response, void* appdata)
{
	PasswordConversation* conv = (PasswordConversation*)appdata;
	if (numMsg <= 0 || numMsg > 4 || conv == NULL)
		return PAM_CONV_ERR;

	struct pam_response* reply = (struct pam_response*)calloc(
		(size_t)numMsg, sizeof(struct pam_response));
	if (reply == NULL)
		return PAM_BUF_ERR;

	for (int i = 0; i < numMsg; i++) {
		switch (message[i]->msg_style) {
			case PAM_PROMPT_ECHO_OFF:
				reply[i].resp = strdup(conv->password != NULL
					? conv->password : "");
				if (reply[i].resp == NULL) {
					free(reply);
					return PAM_BUF_ERR;
				}
				break;
			case PAM_PROMPT_ECHO_ON:
			case PAM_ERROR_MSG:
			case PAM_TEXT_INFO:
				break;
			default:
				free(reply);
				return PAM_CONV_ERR;
		}
	}

	*response = reply;
	return PAM_SUCCESS;
}


static bool
pam_authenticate_user(const char* user, const char* password)
{
	struct pam_conv conv;
	PasswordConversation data;
	data.password = password;
	conv.conv = pam_conversation;
	conv.appdata_ptr = &data;

	pam_handle_t* handle = NULL;
	int r = pam_start(kPamStack, user, &conv, &handle);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "vos-polkit-grant-helper: pam_start: %s\n",
			pam_strerror(NULL, r));
		return false;
	}

	r = pam_authenticate(handle, 0);
	if (r == PAM_SUCCESS)
		r = pam_acct_mgmt(handle, 0);
	if (r != PAM_SUCCESS)
		fprintf(stderr, "vos-polkit-grant-helper: authentication "
			"failed for %s\n", user);

	pam_end(handle, r);
	return r == PAM_SUCCESS;
}


static bool
user_is_admin(const char* user)
{
	struct passwd* pw = getpwnam(user);
	if (pw == NULL)
		return false;

	int count = 512;
	gid_t* gids = (gid_t*)calloc((size_t)count, sizeof(gid_t));
	if (gids == NULL)
		return false;

	if (getgrouplist(user, pw->pw_gid, gids, &count) < 0) {
		free(gids);
		gids = (gid_t*)calloc((size_t)count, sizeof(gid_t));
		if (gids == NULL)
			return false;
		if (getgrouplist(user, pw->pw_gid, gids, &count) < 0) {
			free(gids);
			return false;
		}
	}

	bool admin = false;
	for (int i = 0; i < count && !admin; i++) {
		struct group* gr = getgrgid(gids[i]);
		if (gr == NULL)
			continue;
		for (size_t g = 0; g < sizeof(kAdminGroups)
				/ sizeof(kAdminGroups[0]); g++) {
			if (strcmp(gr->gr_name, kAdminGroups[g]) == 0)
				admin = true;
		}
	}

	free(gids);
	return admin;
}


// The logind session of the calling agent; polkit resolves the same id
// for every unix-process and bus-name subject it checks, which is what
// scopes the generated rule.
static bool
resolve_session(char* out, size_t outSize)
{
	char* session = NULL;
	if (sd_pid_get_session(getppid(), &session) < 0 || session == NULL) {
		if (sd_pid_get_session(getpid(), &session) < 0
			|| session == NULL)
			return false;
	}

	bool ok = strlen(session) < outSize;
	if (ok)
		strcpy(out, session);
	free(session);
	return ok;
}


static bool
session_belongs_to(const char* session, const char* user)
{
	if (!grant_session_is_sane(session))
		return false;

	char path[256];
	snprintf(path, sizeof(path), "%s/%s", kSessionDir, session);

	char text[4096];
	if (!read_whole_file(path, text, sizeof(text)))
		return false;

	char needle[80];
	snprintf(needle, sizeof(needle), "USER=%s\n", user);
	return strstr(text, needle) != NULL;
}


static int
scan_all_policies(const char** ids, int maxIds)
{
	DIR* dir = opendir(kActionsDir);
	if (dir == NULL)
		return 0;

	int found = 0;
	struct dirent* entry;
	while (found < maxIds && (entry = readdir(dir)) != NULL) {
		size_t length = strlen(entry->d_name);
		if (length < 8
			|| strcmp(entry->d_name + length - 7, ".policy") != 0)
			continue;

		char path[512];
		snprintf(path, sizeof(path), "%s/%s", kActionsDir,
			entry->d_name);
		int fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		struct stat st;
		if (fstat(fd, &st) != 0 || st.st_size > 1 << 20) {
			close(fd);
			continue;
		}
		char* text = (char*)malloc((size_t)st.st_size + 1);
		if (text == NULL) {
			close(fd);
			continue;
		}
		ssize_t total = 0;
		while (total < st.st_size) {
			ssize_t n = read(fd, text + total,
				(size_t)st.st_size - total);
			if (n <= 0)
				break;
			total += n;
		}
		close(fd);
		text[total] = '\0';

		found += grant_scan_always_ask(text, ids + found,
			maxIds - found);
		free(text);
	}
	closedir(dir);
	return found;
}


static int
regenerate_rules()
{
	GrantRecord records[kGrantMaxRecords];
	int count = 0;

	DIR* dir = opendir(kStateDir);
	if (dir != NULL) {
		struct dirent* entry;
		while (count < kGrantMaxRecords
			&& (entry = readdir(dir)) != NULL) {
			size_t length = strlen(entry->d_name);
			if (length < 7
				|| strcmp(entry->d_name + length - 6,
					".state") != 0)
				continue;

			char path[512];
			snprintf(path, sizeof(path), "%s/%s", kStateDir,
				entry->d_name);

			char text[512];
			if (!read_whole_file(path, text, sizeof(text)))
				continue;

			GrantRecord record;
			if (!grant_parse_state(text, record))
				continue;
			if (record.expiresUnix <= (int64_t)time(NULL)) {
				unlink(path);
				continue;
			}
			records[count++] = record;
		}
		closedir(dir);
	}

	if (count == 0) {
		unlink(kRulesPath);
		return 0;
	}

	const char* alwaysAsk[kGrantMaxAlwaysAsk];
	int alwaysAskCount = scan_all_policies(alwaysAsk, kGrantMaxAlwaysAsk);

	static char rules[16384];
	int length = grant_render_rules(records, count, alwaysAsk,
		alwaysAskCount, rules, sizeof(rules), (int64_t)time(NULL));
	for (int i = 0; i < alwaysAskCount; i++)
		free((void*)alwaysAsk[i]);
	if (length < 0) {
		syslog(LOG_AUTH | LOG_ERR, "vos-polkit-grant-helper: rules "
			"render failed");
		return -1;
	}

	mkdir(kRulesDir, 0755);

	char tmpPath[512];
	snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", kRulesPath);
	int fd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		syslog(LOG_AUTH | LOG_ERR, "vos-polkit-grant-helper: cannot "
			"write %s: %s", tmpPath, strerror(errno));
		return -1;
	}
	ssize_t written = write(fd, rules, (size_t)length);
	close(fd);
	if (written != length) {
		unlink(tmpPath);
		return -1;
	}
	if (rename(tmpPath, kRulesPath) != 0) {
		syslog(LOG_AUTH | LOG_ERR, "vos-polkit-grant-helper: rename "
			"failed: %s", strerror(errno));
		unlink(tmpPath);
		return -1;
	}
	return 0;
}


static void
revoke_session(const char* session, const char* reason)
{
	char path[512];
	state_path_for(session, path, sizeof(path));
	unlink(path);
	regenerate_rules();
	syslog(LOG_AUTH | LOG_NOTICE, "vos-polkit-grant-helper: grant for "
		"session %s revoked (%s)", session, reason);
}


static void
revoke_all(const char* reason)
{
	DIR* dir = opendir(kStateDir);
	if (dir != NULL) {
		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			size_t length = strlen(entry->d_name);
			if (length < 7
				|| strcmp(entry->d_name + length - 6,
					".state") != 0)
				continue;
			char path[512];
			snprintf(path, sizeof(path), "%s/%s", kStateDir,
				entry->d_name);
			unlink(path);
		}
		closedir(dir);
	}
	regenerate_rules();
	syslog(LOG_AUTH | LOG_NOTICE, "vos-polkit-grant-helper: all grants "
		"revoked (%s)", reason);
}


static bool
mtime_unchanged(const char* path, const struct stat* baseline)
{
	struct stat now;
	if (stat(path, &now) != 0)
		return false;
	return now.st_mtim.tv_sec == baseline->st_mtim.tv_sec
		&& now.st_mtim.tv_nsec == baseline->st_mtim.tv_nsec
		&& now.st_size == baseline->st_size;
}


// Lives until the grant dies on its own: expiry, logout (the session
// record disappears), or the user's password/admin group changing.
// Suspend, lock and session switch are the agent's job; this is the
// backstop that works even when the agent is gone.
static void
watch_grant(const char* session)
{
	char sessionPath[256];
	snprintf(sessionPath, sizeof(sessionPath), "%s/%s", kSessionDir,
		session);

	struct stat baselines[3];
	bool tracked[3];
	for (size_t i = 0; i < 3; i++)
		tracked[i] = stat(kWatchedEtcFiles[i], &baselines[i]) == 0;

	for (;;) {
		sleep(5);

		char statePath[512];
		state_path_for(session, statePath, sizeof(statePath));

		char text[512];
		GrantRecord record;
		if (!read_whole_file(statePath, text, sizeof(text))
			|| !grant_parse_state(text, record)) {
			// Someone else already revoked it.
			break;
		}

		if (record.expiresUnix <= (int64_t)time(NULL)) {
			revoke_session(session, "expired");
			break;
		}

		struct stat st;
		if (stat(sessionPath, &st) != 0) {
			revoke_session(session, "session ended");
			break;
		}

		for (size_t i = 0; i < 3; i++) {
			if (tracked[i]
				&& !mtime_unchanged(kWatchedEtcFiles[i],
					&baselines[i])) {
				revoke_all("credentials changed");
				break;
			}
		}
		if (access(statePath, F_OK) != 0)
			break;
	}
	_exit(0);
}


static void
spawn_watcher(const char* session)
{
	pid_t pid = fork();
	if (pid != 0)
		return;

	setsid();
	int nullFd = open("/dev/null", O_RDWR);
	if (nullFd >= 0) {
		dup2(nullFd, STDIN_FILENO);
		dup2(nullFd, STDOUT_FILENO);
		dup2(nullFd, STDERR_FILENO);
		if (nullFd > STDERR_FILENO)
			close(nullFd);
	}
	watch_grant(session);
}


static bool
read_password(char* out, size_t outSize)
{
	if (fgets(out, (int)outSize, stdin) == NULL)
		return false;
	size_t length = strlen(out);
	if (length > 0 && out[length - 1] == '\n')
		out[--length] = '\0';
	return length > 0;
}


static int
do_grant(const char* user, int minutes)
{
	if (geteuid() != 0) {
		fprintf(stderr, "vos-polkit-grant-helper: not setuid root\n");
		return 1;
	}

	struct passwd* pw = getpwuid(getuid());
	if (pw == NULL || getuid() == 0
		|| strcmp(pw->pw_name, user) != 0) {
		fprintf(stderr, "vos-polkit-grant-helper: grants can only be "
			"recorded for the calling user\n");
		return 1;
	}

	if (minutes != 1 && minutes != 5 && minutes != 15) {
		fprintf(stderr, "vos-polkit-grant-helper: duration must be 1, "
			"5 or 15 minutes\n");
		return 1;
	}

	char session[128];
	if (!resolve_session(session, sizeof(session))
		|| !session_belongs_to(session, user)) {
		fprintf(stderr, "vos-polkit-grant-helper: no usable logind "
			"session for %s\n", user);
		return 1;
	}

	if (!user_is_admin(user)) {
		fprintf(stderr, "vos-polkit-grant-helper: %s is not an "
			"administrator\n", user);
		return 1;
	}

	char password[512];
	if (!read_password(password, sizeof(password)))
		return 1;

	bool ok = pam_authenticate_user(user, password);
	explicit_bzero(password, sizeof(password));
	if (!ok)
		return 1;

	GrantRecord record;
	memset(&record, 0, sizeof(record));
	snprintf(record.user, sizeof(record.user), "%s", user);
	snprintf(record.session, sizeof(record.session), "%s", session);
	record.minutes = minutes;
	record.expiresUnix = (int64_t)time(NULL) + (int64_t)minutes * 60;

	mkdir(kStateDir, 0755);

	char path[512];
	state_path_for(session, path, sizeof(path));
	char text[256];
	if (grant_format_state(record, text, sizeof(text)) < 0)
		return 1;

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "vos-polkit-grant-helper: cannot write %s: "
			"%s\n", path, strerror(errno));
		return 1;
	}
	ssize_t written = write(fd, text, strlen(text));
	close(fd);
	if (written != (ssize_t)strlen(text)) {
		unlink(path);
		return 1;
	}

	if (regenerate_rules() != 0) {
		unlink(path);
		return 1;
	}

	syslog(LOG_AUTH | LOG_NOTICE, "vos-polkit-grant-helper: grant "
		"recorded user=%s session=%s minutes=%d", user, session,
		minutes);
	spawn_watcher(session);
	return 0;
}


static int
do_forget(const char* session)
{
	if (!grant_session_is_sane(session)) {
		fprintf(stderr, "vos-polkit-grant-helper: bad session id\n");
		return 1;
	}

	char path[512];
	state_path_for(session, path, sizeof(path));
	char text[512];
	if (!read_whole_file(path, text, sizeof(text)))
		return 0;	// nothing to forget

	GrantRecord record;
	if (!grant_parse_state(text, record))
		return 1;

	struct passwd* owner = getpwnam(record.user);
	if (getuid() != 0
		&& (owner == NULL || getuid() != owner->pw_uid)) {
		fprintf(stderr, "vos-polkit-grant-helper: only the grant "
			"owner or root may forget\n");
		return 1;
	}

	revoke_session(session, "user request");
	return 0;
}


static void
usage()
{
	fprintf(stderr, "usage: vos-polkit-grant-helper grant <user> "
		"<1|5|15>\n");
	fprintf(stderr, "       vos-polkit-grant-helper forget <session>\n");
}


int
main(int argc, char** argv)
{
	openlog("vos-polkit-grant", LOG_PID, LOG_AUTH);

	if (argc == 4 && strcmp(argv[1], "grant") == 0) {
		int minutes = atoi(argv[3]);
		return do_grant(argv[2], minutes);
	}
	if (argc == 3 && strcmp(argv[1], "forget") == 0)
		return do_forget(argv[2]);

	usage();
	return 1;
}
