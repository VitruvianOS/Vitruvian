/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

extern "C" {
#include <security/pam_appl.h>
#include <systemd/sd-bus.h>
}

#include <AppDefs.h>
#include <Message.h>
#include <OS.h>

#include <kernel/util/KMessage.h>

#include <LaunchDaemonDefs.h>
#include <RegistrarDefs.h>
#include <syscalls.h>


// System-wide root daemons only; login-specific servers are janus_session's
// own fan-out (kFanOutServers in janus_session.cpp).
struct KnownServer {
	const char* name;
	const char* signature;
	const char* port_name;
};

static const KnownServer kKnownServers[] = {
	{ "mount_server",  // root: mount(2) needs CAP_SYS_ADMIN
	  "application/x-vnd.Haiku-mount_server",
	  NULL },
	{ NULL, NULL, NULL }
};


static bool is_graphical_login_allowed(struct passwd* pw);


static const char*
runtime_dir()
{
	static char dir[PATH_MAX] = "";
	if (dir[0] != '\0')
		return dir;

	const char* xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg != NULL && *xdg != '\0')
		snprintf(dir, sizeof(dir), "%s/vos", xdg);
	else
		snprintf(dir, sizeof(dir), "/run/vos");
	mkdir(dir, 0755);
	return dir;
}


struct CurrentSession {
	pid_t   pid;
	char    user[64];
	char    home[PATH_MAX];
	uid_t   uid;
	gid_t   gid;
	bool    greeter;
	char    controlPort[64];	// set once janus_session's HELLO arrives
};

static CurrentSession       sSession = { -1, "", "", (uid_t)-1, (gid_t)-1, false, "" };
static pthread_mutex_t      sSessionLock = PTHREAD_MUTEX_INITIALIZER;

static bool  sSystemMode = false;
static bool  sGreeterMode = false;


static bool
resolve_initial_identity(char* outUser, size_t outUserSize, bool* outGreeter)
{
	if (getuid() != 0) {
		struct passwd* pw = getpwuid(getuid());
		if (pw == NULL)
			return false;
		strlcpy(outUser, pw->pw_name, outUserSize);
		*outGreeter = false;
		sSystemMode = false;
		return true;
	}

	sSystemMode = true;

	char autoName[64] = "";
	FILE* alf = fopen("/etc/vos/autologin", "r");
	if (alf != NULL) {
		if (fgets(autoName, sizeof(autoName), alf) != NULL) {
			size_t len = strlen(autoName);
			while (len > 0 && (autoName[len - 1] == '\n'
					|| autoName[len - 1] == '\r'
					|| autoName[len - 1] == ' '
					|| autoName[len - 1] == '\t'))
				autoName[--len] = '\0';
		}
		fclose(alf);
	}

	if (autoName[0] != '\0' && is_graphical_login_allowed(getpwnam(autoName))) {
		strlcpy(outUser, autoName, outUserSize);
		*outGreeter = false;
	} else if (sGreeterMode) {
		strlcpy(outUser, "vos_login", outUserSize);
		*outGreeter = true;
	} else {
		const char* name = getenv("VOS_DEFAULT_USER");
		if (name == NULL || *name == '\0')
			name = "vos-live";
		strlcpy(outUser, name, outUserSize);
		*outGreeter = false;
	}
	return true;
}


struct JanusApp {
	char    name[64];
	char    signature[256];
	pid_t   pid;
	port_id port;
};

#define JANUS_MAX_APPS 32
static JanusApp        sApps[JANUS_MAX_APPS];
static int              sAppCount = 0;
static pthread_mutex_t  sAppsLock = PTHREAD_MUTEX_INITIALIZER;


struct PendingLaunch {
	bool      active;
	int       app_idx;
	pid_t     pid;
	int       ready_fd;
	port_id   reply_port;
	int32     reply_token;
	bigtime_t deadline;
};

#define JANUS_MAX_PENDING 8
static PendingLaunch sPending[JANUS_MAX_PENDING];

static const bigtime_t kLaunchReadinessTimeoutUsec = 10 * 1000000LL;

static volatile bool   sRunning       = true;
static volatile bool   sShuttingDown  = false;

static port_id sLaunchPort = -1;

static void janus_handle_shutdown(bool reboot);
static void jdbg(const char* fmt, ...);


static int
find_app_by_sig(const char* sig)
{
	for (int i = 0; i < sAppCount; i++)
		if (strcmp(sApps[i].signature, sig) == 0)
			return i;
	return -1;
}


static void
invalidate_app_by_pid(pid_t pid)
{
	pthread_mutex_lock(&sAppsLock);
	for (int i = 0; i < sAppCount; i++) {
		if (sApps[i].pid == pid) {
			sApps[i].pid  = -1;
			sApps[i].port = -1;
			break;
		}
	}
	pthread_mutex_unlock(&sAppsLock);
}


static void
handle_get_launch_data(BMessage* msg)
{
	const char* signature = NULL;
	if (msg->FindString("name", &signature) != B_OK || signature == NULL) {
		BMessage reply(B_ERROR);
		msg->SendReply(&reply);
		return;
	}

	// janus_session mirrors its fan-out here fire-and-forget, and those are
	// not our children to reap, so a row can outlive the server it names.
	pthread_mutex_lock(&sAppsLock);
	int idx = find_app_by_sig(signature);
	port_id port = -1;
	pid_t   pid  = -1;
	if (idx >= 0 && sApps[idx].port >= 0) {
		port_info info;
		if (get_port_info(sApps[idx].port, &info) == B_OK) {
			port = sApps[idx].port;
			pid  = sApps[idx].pid;
		} else {
			sApps[idx].pid  = -1;
			sApps[idx].port = -1;
		}
	}
	pthread_mutex_unlock(&sAppsLock);

	if (port >= 0) {
		BMessage reply(B_OK);
		reply.AddInt32("port", (int32)port);
		reply.AddInt32("team", (int32)pid);
		msg->SendReply(&reply);
		return;
	}

	for (int i = 0; kKnownServers[i].name != NULL; i++) {
		if (strcmp(kKnownServers[i].signature, signature) == 0
				&& kKnownServers[i].port_name != NULL) {
			port_id port = find_port(kKnownServers[i].port_name);
			if (port >= 0) {
				port_info pInfo;
				if (get_port_info(port, &pInfo) == B_OK) {
					BMessage reply(B_OK);
					reply.AddInt32("port", (int32)port);
					reply.AddInt32("team", (int32)pInfo.team);
					msg->SendReply(&reply);
					return;
				}
			}
			break;
		}
	}

	BMessage reply(B_NAME_NOT_FOUND);
	msg->SendReply(&reply);
}


static void
handle_register_app(BPrivate::KMessage& kmsg)
{
	const char* name = NULL;
	const char* signature = NULL;
	int32 pid = -1;
	int32 port = -1;
	if (kmsg.FindString("name", &name) != B_OK
			|| kmsg.FindString("signature", &signature) != B_OK
			|| kmsg.FindInt32("pid", &pid) != B_OK
			|| kmsg.FindInt32("port", &port) != B_OK
			|| name == NULL || signature == NULL)
		return;

	pthread_mutex_lock(&sAppsLock);
	// Match live rows too: find_app_by_sig() returns the first hit, so a
	// row left behind by a dead server would shadow its replacement.
	int idx = find_app_by_sig(signature);
	if (idx < 0 && sAppCount < JANUS_MAX_APPS)
		idx = sAppCount++;
	if (idx >= 0) {
		strlcpy(sApps[idx].name,      name,      sizeof(sApps[idx].name));
		strlcpy(sApps[idx].signature, signature, sizeof(sApps[idx].signature));
		sApps[idx].pid  = (pid_t)pid;
		sApps[idx].port = (port_id)port;
	}
	pthread_mutex_unlock(&sAppsLock);
}


static void
handle_unregister_app(BPrivate::KMessage& kmsg)
{
	const char* signature = NULL;
	int32 pid = -1;
	if (kmsg.FindString("signature", &signature) != B_OK
			|| kmsg.FindInt32("pid", &pid) != B_OK || signature == NULL)
		return;

	pthread_mutex_lock(&sAppsLock);
	int idx = find_app_by_sig(signature);
	// Only if the pid still matches: a replacement may already have
	// registered, and this late notice must not clear the live row.
	if (idx >= 0 && sApps[idx].pid == (pid_t)pid) {
		sApps[idx].pid  = -1;
		sApps[idx].port = -1;
	}
	pthread_mutex_unlock(&sAppsLock);
}


static void
handle_session_hello(BPrivate::KMessage& kmsg, pid_t sender_pid)
{
	const char* port = NULL;
	int32 uid = -1;
	bool greeter = false;
	if (kmsg.FindString("port", &port) != B_OK || port == NULL
			|| kmsg.FindInt32("uid", &uid) != B_OK) {
		fprintf(stderr, "janus_session HELLO missing fields\n");
		return;
	}
	kmsg.FindBool("greeter", &greeter);

	pthread_mutex_lock(&sSessionLock);
	if (sender_pid != sSession.pid) {
		fprintf(stderr, "janus: HELLO from pid=%d, expected current "
			"session pid=%d; ignoring\n", (int)sender_pid, (int)sSession.pid);
	} else {
		strlcpy(sSession.controlPort, port, sizeof(sSession.controlPort));
		sSession.uid = (uid_t)uid;
		sSession.greeter = greeter;
	}
	pthread_mutex_unlock(&sSessionLock);
}


// System-wide daemons only; login-specific servers go through the current
// janus_session's own local launch port instead. Root-only.
static void
handle_launch_job(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	if (sender_uid != 0) {
		fprintf(stderr, "janus: B_LAUNCH_JOB rejected from uid=%u\n",
			(unsigned)sender_uid);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	const char* name = NULL;
	if (kmsg.FindString("name", &name) != B_OK || name == NULL) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}

	const KnownServer* ks = NULL;
	for (int i = 0; kKnownServers[i].name != NULL; i++) {
		if (strcmp(kKnownServers[i].name, name) == 0) {
			ks = &kKnownServers[i];
			break;
		}
	}
	if (ks == NULL) {
		fprintf(stderr, "janus: unknown server: %s\n", name);
		BPrivate::KMessage reply(B_NAME_NOT_FOUND);
		kmsg.SendReply(&reply);
		return;
	}

	char path[256];
	snprintf(path, sizeof(path), "/system/servers/%s", name);
	if (access(path, X_OK) != 0) {
		snprintf(path, sizeof(path), "/system/apps/%s", name);
		if (access(path, X_OK) != 0) {
			snprintf(path, sizeof(path), "/system/%s", name);
			if (access(path, X_OK) != 0) {
				fprintf(stderr, "janus: binary not found: %s\n", name);
				BPrivate::KMessage reply(B_ERROR);
				kmsg.SendReply(&reply);
				return;
			}
		}
	}

	int fds[2];
	if (pipe(fds) != 0) {
		perror("janus: pipe");
		BPrivate::KMessage reply(B_ERROR);
		kmsg.SendReply(&reply);
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);

	pid_t pid = fork();
	if (pid < 0) {
		perror("janus: fork");
		close(fds[0]);
		close(fds[1]);
		BPrivate::KMessage reply(B_ERROR);
		kmsg.SendReply(&reply);
		return;
	}

	if (pid == 0) {
		close(fds[0]);

		char logpath[256];
		snprintf(logpath, sizeof(logpath), "/var/log/%s.log", name);
		int logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (logfd >= 0) {
			dup2(logfd, STDOUT_FILENO);
			dup2(logfd, STDERR_FILENO);
			close(logfd);
		}

		char buf[32];
		snprintf(buf, sizeof(buf), "%d", fds[1]);
		setenv("JANUS_READY_FD", buf, 1);

		setenv("XDG_SESSION_TYPE",    "vos",       1);
		setenv("XDG_DATA_DIRS",   "/system/data:/usr/local/share:/usr/share", 0);
		setenv("XDG_CONFIG_DIRS", "/system/settings:/etc/xdg",                0);

		execl(path, name, NULL);
		fprintf(stderr, "janus: execl(%s) failed: %s\n", path, strerror(errno));
		_exit(127);
	}

	close(fds[1]);

	pthread_mutex_lock(&sAppsLock);
	int app_idx = -1;
	for (int i = 0; i < sAppCount; i++) {
		if (sApps[i].pid == -1
				&& strcmp(sApps[i].signature, ks->signature) == 0) {
			app_idx = i;
			break;
		}
	}
	if (app_idx < 0 && sAppCount < JANUS_MAX_APPS)
		app_idx = sAppCount++;
	if (app_idx >= 0) {
		strlcpy(sApps[app_idx].name,      name,          sizeof(sApps[app_idx].name));
		strlcpy(sApps[app_idx].signature, ks->signature, sizeof(sApps[app_idx].signature));
		sApps[app_idx].pid  = pid;
		sApps[app_idx].port = -1;
	}
	pthread_mutex_unlock(&sAppsLock);

	if (app_idx < 0) {
		fprintf(stderr, "janus: sApps[] full\n");
		close(fds[0]);
		BPrivate::KMessage reply(B_NO_MEMORY);
		kmsg.SendReply(&reply);
		return;
	}

	port_id replyPort  = kmsg.ReplyPort();
	int32   replyToken = kmsg.ReplyToken();

	for (int i = 0; i < JANUS_MAX_PENDING; i++) {
		if (!sPending[i].active) {
			sPending[i].active      = true;
			sPending[i].app_idx     = app_idx;
			sPending[i].pid         = pid;
			sPending[i].ready_fd    = fds[0];
			sPending[i].reply_port  = replyPort;
			sPending[i].reply_token = replyToken;
			sPending[i].deadline    = system_time() + kLaunchReadinessTimeoutUsec;
			printf("janus: launched %s pid=%d, awaiting readiness\n",
				name, (int)pid);
			return;
		}
	}

	fprintf(stderr, "janus: sPending[] full for %s, replying without port\n", name);
	close(fds[0]);
	BPrivate::KMessage reply(B_OK);
	reply.AddInt32("pid", (int32)pid);
	reply.SendTo(replyPort, replyToken);
}


static int
pam_auth_conv(int num, const struct pam_message** msg,
	struct pam_response** resp, void* data)
{
	const char* password = (const char*)data;
	if (num <= 0 || msg == NULL || resp == NULL)
		return PAM_CONV_ERR;

	struct pam_response* r = (struct pam_response*)calloc(num,
		sizeof(struct pam_response));
	if (r == NULL)
		return PAM_BUF_ERR;

	for (int i = 0; i < num; i++) {
		const char* answer = NULL;
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
			answer = (password != NULL) ? password : "";
		else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
			answer = "";
		if (answer == NULL)
			continue;
		r[i].resp = strdup(answer);
		if (r[i].resp == NULL) {
			for (int j = 0; j < i; j++)
				free(r[j].resp);
			free(r);
			return PAM_BUF_ERR;
		}
	}
	*resp = r;
	return PAM_SUCCESS;
}


static char      sAuthenticatedUser[64] = "";
static bigtime_t sAuthenticatedAt = 0;
static const bigtime_t kAuthWindowUsec = 30 * 1000000LL;


static bool
is_live_persona_allowed()
{
	struct stat st;
	if (stat("/etc/vos/live", &st) != 0)
		return false;
	if (stat("/var/lib/vos/first-boot-done", &st) == 0)
		return false;
	return true;
}


static bool
is_graphical_login_allowed(struct passwd* pw)
{
	if (pw == NULL)
		return false;
	if (pw->pw_uid == 0)
		return false;
	if (strcmp(pw->pw_name, "vos_login") == 0)
		return false;
	if (pw->pw_shell != NULL
			&& (strstr(pw->pw_shell, "nologin") != NULL
				|| strstr(pw->pw_shell, "/false") != NULL))
		return false;
	if (strcmp(pw->pw_name, "vos-live") == 0)
		return is_live_persona_allowed();
	if (pw->pw_uid < 1000)
		return false;
	return true;
}


// Credential verification only (pam_authenticate/pam_acct_mgmt); never
// opens a session.
static bool
verify_password(const char* username, const char* password)
{
	if (username == NULL || *username == '\0' || password == NULL)
		return false;
	if (!is_graphical_login_allowed(getpwnam(username))) {
		fprintf(stderr, "janus: graphical login refused for %s\n", username);
		return false;
	}

	struct pam_conv conv = { pam_auth_conv, (void*)password };
	pam_handle_t* h = NULL;
	int r = pam_start("vitruvian-auth", username, &conv, &h);
	if (r != PAM_SUCCESS)
		return false;

	// PAM_TTY must be a real device name (not ":0"), or pam_faillock keys
	// every failure across every seat into a single bucket.
	pam_set_item(h, PAM_RUSER, "vos_login");
	pam_set_item(h, PAM_RHOST, "");
	pam_set_item(h, PAM_TTY,   "tty1");

	r = pam_authenticate(h, 0);
	if (r == PAM_SUCCESS)
		r = pam_acct_mgmt(h, 0);
	bool ok = (r == PAM_SUCCESS);
	if (!ok)
		fprintf(stderr, "janus: PAM auth for %s: %s\n", username,
			pam_strerror(h, r));

	pam_end(h, r);
	return ok;
}


static void
copy_attributes(int srcFd, int dstFd)
{
	ssize_t listSize = flistxattr(srcFd, NULL, 0);
	if (listSize <= 0)
		return;

	char* names = (char*)malloc(listSize);
	if (names == NULL)
		return;

	listSize = flistxattr(srcFd, names, listSize);
	if (listSize <= 0) {
		free(names);
		return;
	}

	static const char kPrefix[] = "user.beos.";
	for (ssize_t i = 0; i < listSize; i += strlen(names + i) + 1) {
		const char* name = names + i;
		if (strncmp(name, kPrefix, sizeof(kPrefix) - 1) != 0)
			continue;

		ssize_t size = fgetxattr(srcFd, name, NULL, 0);
		if (size < 0)
			continue;

		char* value = (char*)malloc(size > 0 ? size : 1);
		if (value == NULL)
			continue;
		if (fgetxattr(srcFd, name, value, size) == size)
			fsetxattr(dstFd, name, value, size, 0);
		free(value);
	}

	free(names);
}


static void
copy_settings_file(const char* srcPath, const char* dstDir,
	const char* fileName, uid_t uid, gid_t gid)
{
	int src = open(srcPath, O_RDONLY | O_CLOEXEC);
	if (src < 0)
		return;

	if (mkdir(dstDir, 0755) < 0 && errno != EEXIST) {
		close(src);
		return;
	}
	if (chown(dstDir, uid, gid) < 0) { /* best effort */ }

	char dstPath[PATH_MAX];
	snprintf(dstPath, sizeof(dstPath), "%s/%s", dstDir, fileName);
	int dst = open(dstPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (dst < 0) {
		close(src);
		return;
	}

	char buf[8192];
	ssize_t n;
	while ((n = read(src, buf, sizeof(buf))) > 0) {
		ssize_t off = 0;
		while (off < n) {
			ssize_t w = write(dst, buf + off, n - off);
			if (w < 0) { if (errno == EINTR) continue; goto done; }
			off += w;
		}
	}
done:
	copy_attributes(src, dst);
	close(src);
	close(dst);
	if (chown(dstPath, uid, gid) < 0) { /* best effort */ }
}


static void
seed_user_settings_from_preauth(const char* userHome, uid_t uid, gid_t gid)
{
	if (userHome == NULL || userHome[0] == '\0')
		return;

	char configDir[PATH_MAX];
	char settingsDir[PATH_MAX];
	snprintf(configDir,   sizeof(configDir),   "%s/config", userHome);
	snprintf(settingsDir, sizeof(settingsDir), "%s/config/settings", userHome);

	if (mkdir(configDir, 0755) < 0 && errno != EEXIST) return;
	if (chown(configDir, uid, gid) < 0) { /* best effort */ }

	if (mkdir(settingsDir, 0755) < 0 && errno != EEXIST) return;
	if (chown(settingsDir, uid, gid) < 0) { /* best effort */ }

	static const char* kSrcDir = "/system/login/config/settings";

	static const struct {
		const char*	subDir;		// relative to settings/, "" for none
		const char*	name;
	} kFiles[] = {
		{ "",		"Locale settings" },
		{ "",		"Key_map" },
		{ "input",	"layout" },
		{ "input",	"xkb_layout" },
	};

	for (size_t i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); i++) {
		char src[PATH_MAX];
		char dstDir[PATH_MAX];

		if (kFiles[i].subDir[0] != '\0') {
			snprintf(src, sizeof(src), "%s/%s/%s", kSrcDir,
				kFiles[i].subDir, kFiles[i].name);
			snprintf(dstDir, sizeof(dstDir), "%s/config/settings/%s",
				userHome, kFiles[i].subDir);
		} else {
			snprintf(src, sizeof(src), "%s/%s", kSrcDir, kFiles[i].name);
			snprintf(dstDir, sizeof(dstDir), "%s", settingsDir);
		}

		copy_settings_file(src, dstDir, kFiles[i].name, uid, gid);
	}
}


static void
jdbg(const char* fmt, ...)
{
	FILE* f = fopen("/var/log/janus-dbg.log", "a");
	if (f == NULL)
		return;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	fprintf(f, "[%ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fflush(f);
	fsync(fileno(f));
	fclose(f);
}


static pid_t
fork_janus_session(const char* user, bool greeter)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "janus: fork(janus_session) for %s: %s\n",
			user, strerror(errno));
		return -1;
	}
	if (pid == 0) {
		setenv("JANUS_SESSION_USER", user, 1);
		if (greeter)
			setenv("JANUS_SESSION_GREETER", "1", 1);
		else
			unsetenv("JANUS_SESSION_GREETER");
		// argv is repeated here only so janus_session has room to rewrite
		// its process title into /proc/<pid>/cmdline.
		execl("/system/servers/janus_session", "janus_session", user,
			greeter ? "greeter" : "session", NULL);
		fprintf(stderr, "janus: execl(janus_session) failed: %s\n",
			strerror(errno));
		_exit(127);
	}
	printf("janus: forked janus_session pid=%d for %s (greeter=%d)\n",
		(int)pid, user, (int)greeter);
	return pid;
}


static void
terminate_and_wait_session(pid_t pid)
{
	if (pid <= 0)
		return;

	kill(pid, SIGTERM);
	for (int wait_ms = 0; wait_ms < 5000; wait_ms += 20) {
		int status = 0;
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid || (r < 0 && errno == ECHILD))
			return;
		usleep(20 * 1000);
	}
	fprintf(stderr, "janus: janus_session pid=%d slow to exit; SIGKILL\n",
		(int)pid);
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
}


// A dead login's ports must not survive into the next one's
// B_GET_LAUNCH_DATA answers.
static void
purge_session_apps()
{
	pthread_mutex_lock(&sAppsLock);
	int keep = 0;
	for (int i = 0; i < sAppCount; i++) {
		bool owned = false;
		for (int k = 0; kKnownServers[k].name != NULL; k++) {
			if (strcmp(sApps[i].name, kKnownServers[k].name) == 0) {
				owned = true;
				break;
			}
		}
		if (owned) {
			if (i != keep)
				sApps[keep] = sApps[i];
			keep++;
		}
	}
	sAppCount = keep;
	pthread_mutex_unlock(&sAppsLock);
}


struct SessionTransition {
	char user[64];
	bool greeter;
};

static void*
session_transition_thread(void* arg)
{
	SessionTransition* t = (SessionTransition*)arg;

	pthread_mutex_lock(&sSessionLock);
	pid_t oldPid = sSession.pid;
	pthread_mutex_unlock(&sSessionLock);

	// Two janus_sessions alive at once would race libseat over the seat.
	terminate_and_wait_session(oldPid);
	purge_session_apps();

	pid_t newPid = fork_janus_session(t->user, t->greeter);

	pthread_mutex_lock(&sSessionLock);
	sSession.pid = newPid;
	strlcpy(sSession.user, t->user, sizeof(sSession.user));
	sSession.greeter = t->greeter;
	sSession.controlPort[0] = '\0';
	struct passwd* pw = getpwnam(t->user);
	if (pw != NULL) {
		strlcpy(sSession.home, pw->pw_dir, sizeof(sSession.home));
		sSession.uid = pw->pw_uid;
		sSession.gid = pw->pw_gid;
	}
	pthread_mutex_unlock(&sSessionLock);

	sGreeterMode = t->greeter;

	delete t;
	return NULL;
}


static void
spawn_session_transition(const char* user, bool greeter)
{
	SessionTransition* t = new SessionTransition();
	strlcpy(t->user, user, sizeof(t->user));
	t->greeter = greeter;

	pthread_t th;
	if (pthread_create(&th, NULL, session_transition_thread, t) != 0) {
		fprintf(stderr, "janus: session_transition_thread: %s\n", strerror(errno));
		delete t;
		return;
	}
	pthread_detach(th);
}


static volatile bool sShutdownInFlight = false;


static void*
logind_watch_thread(void* /*arg*/)
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return NULL;

	sd_bus_match_signal(bus, NULL, "org.freedesktop.login1",
		"/org/freedesktop/login1", "org.freedesktop.login1.Manager",
		"PrepareForShutdown", NULL, NULL);

	while (sRunning) {
		sd_bus_message* m = NULL;
		int r = sd_bus_process(bus, &m);
		if (r < 0)
			break;
		if (m != NULL) {
			const char* member = sd_bus_message_get_member(m);
			int active = 0;
			if (member != NULL
					&& strcmp(member, "PrepareForShutdown") == 0
					&& sd_bus_message_read(m, "b", &active) >= 0
					&& active != 0) {
				sShutdownInFlight = true;
			}
			sd_bus_message_unref(m);
		}
		if (r == 0)
			sd_bus_wait(bus, 500000);
	}
	sd_bus_unref(bus);
	return NULL;
}


static void
handle_switch_vt(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	int32 vt = 0;
	if (kmsg.FindInt32("vt", &vt) != B_OK || vt < 1 || vt > 63) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}

	pthread_mutex_lock(&sSessionLock);
	bool allowed = sender_uid == sSession.uid;
	char controlPort[64];
	strlcpy(controlPort, sSession.controlPort, sizeof(controlPort));
	pthread_mutex_unlock(&sSessionLock);

	if (!allowed) {
		fprintf(stderr, "janus: B_JANUS_SWITCH_VT rejected from uid=%u\n",
			(unsigned)sender_uid);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}
	if (controlPort[0] == '\0') {
		BPrivate::KMessage reply(B_NO_INIT);
		kmsg.SendReply(&reply);
		return;
	}

	port_id port = find_port(controlPort);
	if (port < 0) {
		BPrivate::KMessage reply(B_NO_INIT);
		kmsg.SendReply(&reply);
		return;
	}

	jdbg("handle_switch_vt() relaying vt=%d to %s", (int)vt, controlPort);

	BPrivate::KMessage relay(BPrivate::B_JANUS_SWITCH_VT);
	relay.AddInt32("vt", vt);
	BPrivate::KMessage relayReply;
	status_t err = relay.SendTo(port, -1, &relayReply, 2000000LL, 2000000LL,
		getpid());

	BPrivate::KMessage reply(err == B_OK ? relayReply.What() : B_ERROR);
	kmsg.SendReply(&reply);
}


static void
handle_logout(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	pthread_mutex_lock(&sSessionLock);
	uid_t sessionUid = sSession.uid;
	bool  greeter    = sSession.greeter;
	pthread_mutex_unlock(&sSessionLock);

	jdbg("handle_logout() ENTER sender_uid=%u session_uid=%u shutdownInFlight=%d",
		(unsigned)sender_uid, (unsigned)sessionUid, (int)sShutdownInFlight);
	printf("janus: B_JANUS_LOGOUT received from uid=%u (session uid=%u)\n",
		(unsigned)sender_uid, (unsigned)sessionUid);

	if (greeter || sessionUid == (uid_t)-1 || sender_uid != sessionUid) {
		fprintf(stderr, "janus: B_JANUS_LOGOUT rejected sender_uid=%u "
			"(session uid=%u)\n",
			(unsigned)sender_uid, (unsigned)sessionUid);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	if (sShutdownInFlight) {
		fprintf(stderr, "janus: B_JANUS_LOGOUT ignored (shutdown in flight)\n");
		BPrivate::KMessage reply(B_BUSY);
		kmsg.SendReply(&reply);
		return;
	}

	printf("janus: LOGOUT — tearing down session for uid=%u\n",
		(unsigned)sessionUid);

	BPrivate::KMessage reply(B_OK);
	kmsg.SendReply(&reply);

	spawn_session_transition("vos_login", true);
}


static void
handle_login_ok(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	struct passwd* vl = getpwnam("vos_login");
	uid_t allowed = (vl != NULL) ? vl->pw_uid : (uid_t)-1;
	if (allowed == (uid_t)-1 || sender_uid != allowed) {
		fprintf(stderr, "janus: B_JANUS_LOGIN_OK rejected from uid=%u "
			"(expected vos_login=%u)\n",
			(unsigned)sender_uid, (unsigned)allowed);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	const char* user = NULL;
	if (kmsg.FindString("user", &user) != B_OK || user == NULL) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}

	const char* mode = NULL;
	bool livePath = kmsg.FindString("mode", &mode) == B_OK
		&& mode != NULL && strcmp(mode, "live") == 0
		&& strcmp(user, "vos-live") == 0;

	if (livePath) {
		if (!is_live_persona_allowed()) {
			fprintf(stderr, "janus: live LOGIN_OK refused — persona "
				"not enabled on this root\n");
			BPrivate::KMessage reply(B_NOT_ALLOWED);
			kmsg.SendReply(&reply);
			return;
		}
	} else {
		bigtime_t now = system_time();
		if (sAuthenticatedUser[0] == '\0'
				|| strcmp(sAuthenticatedUser, user) != 0
				|| (now - sAuthenticatedAt) > kAuthWindowUsec) {
			fprintf(stderr, "janus: LOGIN_OK for %s without matching "
				"AUTH\n", user);
			sAuthenticatedUser[0] = '\0';
			sAuthenticatedAt = 0;
			BPrivate::KMessage reply(B_NOT_ALLOWED);
			kmsg.SendReply(&reply);
			return;
		}
		sAuthenticatedUser[0] = '\0';
		sAuthenticatedAt = 0;
	}

	struct passwd* pw = getpwnam(user);
	if (pw == NULL) {
		fprintf(stderr, "janus: LOGIN_OK for unknown user %s\n", user);
		BPrivate::KMessage reply(B_NAME_NOT_FOUND);
		kmsg.SendReply(&reply);
		return;
	}
	if (!is_graphical_login_allowed(pw)) {
		fprintf(stderr, "janus: LOGIN_OK refused for %s (root/system)\n", user);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	printf("janus: LOGIN_OK — switching session to %s (uid=%u)\n",
		user, (unsigned)pw->pw_uid);

	BPrivate::KMessage reply(B_OK);
	kmsg.SendReply(&reply);

	seed_user_settings_from_preauth(pw->pw_dir, pw->pw_uid, pw->pw_gid);

	spawn_session_transition(user, false);
}


static void
handle_auth_request(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	struct passwd* vl = getpwnam("vos_login");
	uid_t allowed = (vl != NULL) ? vl->pw_uid : (uid_t)-1;
	if (allowed == (uid_t)-1 || sender_uid != allowed) {
		fprintf(stderr, "janus: B_JANUS_AUTH_REQUEST rejected from uid=%u "
			"(expected vos_login=%u)\n",
			(unsigned)sender_uid, (unsigned)allowed);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	const char* user = NULL;
	const char* pass = NULL;
	if (kmsg.FindString("user", &user) != B_OK
			|| kmsg.FindString("password", &pass) != B_OK
			|| user == NULL || pass == NULL) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}

	bool ok = verify_password(user, pass);

	explicit_bzero(const_cast<char*>(pass), strlen(pass));

	if (!ok) {
		sAuthenticatedUser[0] = '\0';
		sAuthenticatedAt = 0;
		BPrivate::KMessage reply(B_PERMISSION_DENIED);
		kmsg.SendReply(&reply);
		return;
	}

	strncpy(sAuthenticatedUser, user, sizeof(sAuthenticatedUser) - 1);
	sAuthenticatedUser[sizeof(sAuthenticatedUser) - 1] = '\0';
	sAuthenticatedAt = system_time();

	BPrivate::KMessage reply(B_OK);
	reply.AddString("user", user);
	kmsg.SendReply(&reply);
}


static void
check_pending_launches()
{
	for (int i = 0; i < JANUS_MAX_PENDING; i++) {
		if (!sPending[i].active)
			continue;

		struct pollfd pfd;
		pfd.fd      = sPending[i].ready_fd;
		pfd.events  = POLLIN;
		pfd.revents = 0;
		int ret = poll(&pfd, 1, 0);

		port_id port = -1;
		bool resolved = false;

		if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			ssize_t n = read(sPending[i].ready_fd, &port, sizeof(port));
			resolved = true;
			if (n != (ssize_t)sizeof(port))
				port = -1;
		} else if (system_time() > sPending[i].deadline) {
			int idx = sPending[i].app_idx;
			fprintf(stderr, "janus: readiness timeout for %s\n",
				sApps[idx].name);
			resolved = true;
		}

		if (!resolved)
			continue;

		close(sPending[i].ready_fd);
		int idx = sPending[i].app_idx;

		if (port >= 0) {
			pthread_mutex_lock(&sAppsLock);
			bool stale = sApps[idx].pid != sPending[i].pid;
			if (!stale)
				sApps[idx].port = port;
			pthread_mutex_unlock(&sAppsLock);
			if (stale)
				port = -1;
		}

		if (port >= 0) {

			printf("janus: %s ready, pid=%d port=%d\n",
				sApps[idx].name, (int)sApps[idx].pid, (int)port);

			char pidpath[128];
			snprintf(pidpath, sizeof(pidpath), "%s/%s.pid",
				runtime_dir(), sApps[idx].name);
			FILE* f = fopen(pidpath, "w");
			if (f != NULL) {
				fprintf(f, "%d\n", (int)sApps[idx].pid);
				fclose(f);
			} else {
				fprintf(stderr, "janus: could not write %s: %s\n",
					pidpath, strerror(errno));
			}
		}

		BPrivate::KMessage reply(port >= 0 ? B_OK : B_ERROR);
		if (port >= 0)
			reply.AddInt32("pid", (int32)sApps[idx].pid);
		reply.SendTo(sPending[i].reply_port, sPending[i].reply_token);

		sPending[i].active = false;
	}
}


static int32
launch_daemon_thread(void* /*data*/)
{
	while (sRunning) {
		port_message_info mi = {};
		status_t infoRc = get_port_message_info_etc(sLaunchPort, &mi,
			B_RELATIVE_TIMEOUT, 50000);

		if (infoRc == B_TIMED_OUT || infoRc == B_INTERRUPTED) {
			check_pending_launches();
			continue;
		}
		if (infoRc < B_OK)
			break;

		ssize_t bufSize = (ssize_t)mi.size;
		int32 code = 0;
		void* buf = (bufSize > 0) ? malloc(bufSize) : NULL;

		ssize_t r = read_port(sLaunchPort, &code, buf,
			bufSize > 0 ? bufSize : 0);
		if (r < B_OK) {
			free(buf);
			check_pending_launches();
			continue;
		}

		if (code == 'KMSG') {
			BPrivate::KMessage kmsg;
			if (bufSize > 0)
				kmsg.SetTo((const void*)buf, bufSize);
			if (kmsg.What() == BPrivate::B_LAUNCH_JOB) {
				if (!sShuttingDown)
					handle_launch_job(kmsg, mi.sender);
			} else if (kmsg.What() == BPrivate::B_JANUS_AUTH_REQUEST) {
				if (!sShuttingDown)
					handle_auth_request(kmsg, mi.sender);
			} else if (kmsg.What() == BPrivate::B_JANUS_LOGIN_OK) {
				if (!sShuttingDown)
					handle_login_ok(kmsg, mi.sender);
			} else if (kmsg.What() == BPrivate::B_JANUS_SWITCH_VT) {
				if (!sShuttingDown)
					handle_switch_vt(kmsg, mi.sender);
			} else if (kmsg.What() == BPrivate::B_JANUS_LOGOUT) {
				jdbg("MSG B_JANUS_LOGOUT received (shuttingDown=%d)",
					(int)sShuttingDown);
				if (!sShuttingDown)
					handle_logout(kmsg, mi.sender);
			} else if (kmsg.What() == BPrivate::B_JANUS_SESSION_HELLO) {
				handle_session_hello(kmsg, mi.sender_team);
			} else if (kmsg.What() == BPrivate::B_JANUS_REGISTER_APP) {
				handle_register_app(kmsg);
			} else if (kmsg.What() == BPrivate::B_JANUS_UNREGISTER_APP) {
				handle_unregister_app(kmsg);
			}
		} else {
			BMessage* msg = new BMessage();
			if (bufSize > 0 && msg->Unflatten((const char*)buf) == B_OK) {
				switch (msg->what) {
					case BPrivate::B_GET_LAUNCH_DATA:
						handle_get_launch_data(msg);
						break;
					case BPrivate::B_REG_SHUTDOWN_FINISHED: {
						bool reboot = false;
						msg->FindBool("reboot", &reboot);
						jdbg("MSG B_REG_SHUTDOWN_FINISHED received reboot=%d",
							(int)reboot);
						delete msg;
						msg = NULL;
						free(buf);
						buf = NULL;
						janus_handle_shutdown(reboot);
						break;
					}
					default:
						if (msg->IsSourceWaiting()) {
							BMessage reply(B_NOT_SUPPORTED);
							msg->SendReply(&reply);
						}
						break;
				}
			}
			delete msg;
		}

		free(buf);

		check_pending_launches();
	}
	return 0;
}


static bool
init_launch_daemon_port()
{
	sLaunchPort = create_port(50, B_LAUNCH_DAEMON_PORT_NAME);
	if (sLaunchPort < 0) {
		fprintf(stderr, "janus: failed to create launch daemon port: %s\n",
			strerror(errno));
		return false;
	}

	thread_id t = spawn_thread(launch_daemon_thread, "janus:launch_daemon",
		B_NORMAL_PRIORITY, NULL);
	if (t < 0) {
		fprintf(stderr, "janus: failed to spawn launch daemon thread\n");
		delete_port(sLaunchPort);
		sLaunchPort = -1;
		return false;
	}

	resume_thread(t);
	printf("janus: launch daemon port created (port=%d)\n", (int)sLaunchPort);
	return true;
}


// Only the greeter-enabled path: the non-greeter path has its own systemd
// unit that execs janus_launch instead.
static void
spawn_mount_server()
{
	if (!sGreeterMode)
		return;

	BPrivate::KMessage req(BPrivate::B_LAUNCH_JOB);
	req.AddString("name", "mount_server");
	BPrivate::KMessage reply;
	req.SendTo(sLaunchPort, -1, &reply, 5000000LL, 15000000LL, getpid());
}


static void
sig_handler(int /*sig*/)
{
	sRunning = false;
}


static void
daemon_loop()
{
	while (sRunning) {
		usleep(200 * 1000);

		pid_t reaped;
		while (!sShuttingDown && (reaped = waitpid(-1, NULL, WNOHANG)) > 0)
			invalidate_app_by_pid(reaped);
	}
}


static void
janus_handle_shutdown(bool reboot)
{
	jdbg("janus_handle_shutdown() ENTER reboot=%d", reboot);
	fprintf(stderr, "janus: shutdown handover received, reboot=%d\n", reboot);

	int shutdownMarkerFd = open("/run/vos/shutting-down",
		O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
	if (shutdownMarkerFd >= 0)
		close(shutdownMarkerFd);

	// Set BEFORE _kern_shutdown so the poll-loop reaper doesn't steal the
	// systemctl child's waitpid status.
	sShuttingDown = true;

	pthread_mutex_lock(&sSessionLock);
	pid_t currentPid = sSession.pid;
	sSession.pid = -1;
	pthread_mutex_unlock(&sSessionLock);
	terminate_and_wait_session(currentPid);

	sync();

	status_t status = _kern_shutdown(reboot);
	if (status != B_OK) {
		fprintf(stderr, "janus: _kern_shutdown failed (0x%x); "
			"returning to main loop so systemd can retry\n", status);
		unlink("/run/vos/shutting-down");
		sShuttingDown = false;
		return;
	}
}


int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	signal(SIGTERM, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGCHLD, SIG_DFL);

	runtime_dir();

	if (access("/var/lib/vos/greeter-enabled", F_OK) == 0)
		sGreeterMode = true;

	char initialUser[64] = "";
	bool initialGreeter = sGreeterMode;
	bool haveIdentity = resolve_initial_identity(initialUser,
		sizeof(initialUser), &initialGreeter);
	if (!haveIdentity) {
		fprintf(stderr, "janus: initial login identity not resolved "
			"(user-mode: getpwuid failed; system-mode: check "
			"VOS_DEFAULT_USER)\n");
	}
	sGreeterMode = initialGreeter;

	if (!init_launch_daemon_port())
		return 1;

	{
		pthread_t th;
		if (pthread_create(&th, NULL, logind_watch_thread, NULL) == 0)
			pthread_detach(th);
		else
			fprintf(stderr, "janus: logind_watch_thread: %s\n",
				strerror(errno));
	}

	spawn_mount_server();

	if (sSystemMode && haveIdentity) {
		// A janus restarted during a reboot must not flash the greeter.
		if (access("/run/vos/shutting-down", F_OK) == 0) {
			jdbg("startup: shutdown in progress — NOT spawning session chain");
			fprintf(stderr, "janus: shutdown in progress; not spawning session\n");
		} else {
			printf("janus: spawning initial janus_session for %s "
				"(greeter=%d)\n", initialUser, (int)initialGreeter);
			spawn_session_transition(initialUser, initialGreeter);
		}
	} else if (!sSystemMode && haveIdentity) {
		spawn_session_transition(initialUser, false);
	}

	daemon_loop();

	sRunning = false;

	pthread_mutex_lock(&sSessionLock);
	pid_t currentPid = sSession.pid;
	sSession.pid = -1;
	pthread_mutex_unlock(&sSessionLock);
	terminate_and_wait_session(currentPid);

	if (sLaunchPort >= 0) {
		delete_port(sLaunchPort);
		sLaunchPort = -1;
	}
	return 0;
}
