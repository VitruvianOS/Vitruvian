/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

// janus_session — bounded-lifetime login layer of the janus split; forked
// fresh per login, owns that login's PAM session, seat, and fan-out.
// See janus-session-leader-redesign-plan.md for the full design.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libseat.h>
#include <security/pam_appl.h>
}

#include <AppDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <kernel/util/KMessage.h>

#include <LaunchDaemonDefs.h>
#include <MessengerPrivate.h>
#include <RegistrarDefs.h>
#include <syscalls.h>


// The login's own fan-out, direct children of this process. Excludes
// mount_server, which the supervisor owns instead.
struct KnownServer {
	const char* name;
	const char* signature;
	bool        needs_drm;
};

static const KnownServer kFanOutServers[] = {
	{ "registrar",           B_REGISTRAR_SIGNATURE,                     false },
	{ "app_server",          "application/x-vnd.Haiku-app_server",      true  },
	{ "input_server",        "application/x-vnd.Be-input_server",       false },
	{ "notification_server", "application/x-vnd.Haiku-notification_server", false },
	{ "vos-polkit-agent",    "application/x-vnd.Vitruvian-polkit-agent", false },
	{ "Deskbar",             "application/x-vnd.Be-TSKB",               false },
	{ "Tracker",             "application/x-vnd.Be-TRAK",               false },
	{ "vitruvian-login",     "application/x-vnd.Vitruvian-login",       false },
	{ "FirstBootPrompt",     "application/x-vnd.Haiku-FirstBootPrompt", false },
	{ NULL, NULL, false }
};


static char  sUserName[64]       = "";
static char  sUserHome[PATH_MAX] = "";
static uid_t sUserUid            = (uid_t)-1;
static gid_t sUserGid            = (gid_t)-1;
static bool  sGreeterMode        = false;
static char  sRuntimeDir[PATH_MAX] = "";


static const char*
runtime_dir()
{
	if (sRuntimeDir[0] != '\0')
		return sRuntimeDir;
	snprintf(sRuntimeDir, sizeof(sRuntimeDir), "/run/vos");
	mkdir(sRuntimeDir, 0755);
	return sRuntimeDir;
}


// Reads the identity the supervisor decided on; never re-derives login
// policy itself.
static bool
resolve_identity()
{
	const char* name = getenv("JANUS_SESSION_USER");
	if (name == NULL || *name == '\0') {
		fprintf(stderr, "janus_session: JANUS_SESSION_USER not set\n");
		return false;
	}
	sGreeterMode = getenv("JANUS_SESSION_GREETER") != NULL;

	struct passwd* pw = getpwnam(name);
	if (pw == NULL) {
		fprintf(stderr, "janus_session: getpwnam(%s) failed\n", name);
		return false;
	}
	strlcpy(sUserName, pw->pw_name, sizeof(sUserName));
	strlcpy(sUserHome, pw->pw_dir,  sizeof(sUserHome));
	sUserUid = pw->pw_uid;
	sUserGid = pw->pw_gid;
	return true;
}


// PR_SET_NAME sets the short comm field; argv[] is overwritten in place so
// ps/proc/cmdline show the full "janus_session (<user>)" title.
static void
set_proc_title(int argc, char** argv, const char* title)
{
	prctl(PR_SET_NAME, "janus_session", 0, 0, 0);

	if (argc <= 0 || argv == NULL || argv[0] == NULL)
		return;

	char* start = argv[0];
	char* end = argv[argc - 1] + strlen(argv[argc - 1]) + 1;
	size_t avail = (size_t)(end - start);
	if (avail <= 1)
		return;

	size_t len = strlen(title);
	if (len >= avail)
		len = avail - 1;
	memset(start, 0, avail);
	memcpy(start, title, len);
	for (int i = 1; i < argc; i++)
		argv[i] = start + len;
}


// --- PAM session -----------------------------------------------------
//
// A fresh process opens its own PAM session exactly once, so loginuid is
// unset going in and logind keys a genuine session on THIS pid.

static pam_handle_t* sPamHandle = NULL;
static char**        sPamEnv    = NULL;


static int
pam_null_conv(int num, const struct pam_message**, struct pam_response** resp, void*)
{
	*resp = (struct pam_response*)calloc(num, sizeof(struct pam_response));
	return *resp != NULL ? PAM_SUCCESS : PAM_BUF_ERR;
}


static void
close_pam_session()
{
	if (sPamHandle == NULL)
		return;
	pam_close_session(sPamHandle, 0);
	pam_setcred(sPamHandle, PAM_DELETE_CRED);
	pam_end(sPamHandle, PAM_SUCCESS);
	sPamHandle = NULL;

	if (sPamEnv != NULL) {
		for (char** e = sPamEnv; *e != NULL; e++)
			free(*e);
		free(sPamEnv);
		sPamEnv = NULL;
	}
}


static bool
init_pam_session()
{
	static struct pam_conv conv = { pam_null_conv, NULL };
	const char* stack = sGreeterMode ? "vitruvian-greeter" : "vitruvian-session";
	int r = pam_start(stack, sUserName, &conv, &sPamHandle);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus_session: pam_start failed (%d)\n", r);
		sPamHandle = NULL;
		return false;
	}
	pam_set_item(sPamHandle, PAM_TTY,   "tty1");
	pam_set_item(sPamHandle, PAM_RUSER, "root");

	pam_putenv(sPamHandle, "XDG_SEAT=seat0");
	pam_putenv(sPamHandle, "XDG_VTNR=1");
	pam_putenv(sPamHandle, "XDG_SESSION_TYPE=wayland");

	r = pam_setcred(sPamHandle, PAM_ESTABLISH_CRED);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus_session: pam_setcred failed: %s\n",
			pam_strerror(sPamHandle, r));
		pam_end(sPamHandle, r);
		sPamHandle = NULL;
		return false;
	}

	r = pam_open_session(sPamHandle, 0);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus_session: pam_open_session failed: %s\n",
			pam_strerror(sPamHandle, r));
		pam_setcred(sPamHandle, PAM_DELETE_CRED);
		pam_end(sPamHandle, r);
		sPamHandle = NULL;
		return false;
	}

	sPamEnv = pam_getenvlist(sPamHandle);
	printf("janus_session: PAM session opened for %s\n", sUserName);
	return true;
}


// getgrouplist() goes through NSS which can block a forked child of a
// threaded process; resolve before any fork, hand the result to
// drop_to_user().
static bool
resolve_user_groups(gid_t** _groups, int* _count)
{
	int count = 32;
	gid_t* groups = (gid_t*)malloc(count * sizeof(gid_t));
	if (groups == NULL)
		return false;

	if (getgrouplist(sUserName, sUserGid, groups, &count) < 0) {
		gid_t* resized = (gid_t*)realloc(groups, count * sizeof(gid_t));
		if (resized != NULL) {
			groups = resized;
			if (getgrouplist(sUserName, sUserGid, groups, &count) < 0) {
				groups[0] = sUserGid;
				count = 1;
			}
		} else {
			groups[0] = sUserGid;
			count = 1;
		}
	}
	*_groups = groups;
	*_count = count;
	return true;
}


static bool
drop_to_user(const gid_t* groups, int count)
{
	return setgroups(count, groups) == 0
		&& setgid(sUserGid) == 0
		&& setuid(sUserUid) == 0;
}


// --- App registry / readiness -----------------------------------------
//
// Local mirror scoped to this login's own children; each ready app is also
// reported to the supervisor (register_app_with_supervisor()).

struct JanusApp {
	char    name[64];
	char    signature[256];
	pid_t   pid;
	port_id port;
};

#define JANUS_MAX_APPS 16
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

static port_id sLaunchPort = -1;
static char    sLaunchPortName[64] = "";

// libseat's logind backend drives an sd_bus connection that is not
// thread-safe, so the seat belongs to daemon_loop()'s thread alone.
static struct libseat* sSeat        = NULL;
static int              sDrmFd       = -1;
static int              sDrmDeviceId = -1;
static volatile bool    sSessionActive = false;
static volatile bool    sRunning       = true;
static volatile bool    sShuttingDown  = false;

static int              sSeatWakeFd  = -1;
static pthread_mutex_t  sSeatReqLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   sSeatReqCond = PTHREAD_COND_INITIALIZER;
static int              sSeatReqVt   = 0;
static bool             sSeatReqPending = false;
static bool             sSeatReqDone = true;
static status_t         sSeatReqResult = B_OK;
static volatile bool    sDaemonLoopActive = false;


static status_t
submit_switch_vt(int vt)
{
	pthread_mutex_lock(&sSeatReqLock);

	if (!sDaemonLoopActive || sSeatWakeFd < 0) {
		pthread_mutex_unlock(&sSeatReqLock);
		return B_NO_INIT;
	}

	while (sSeatReqPending)
		pthread_cond_wait(&sSeatReqCond, &sSeatReqLock);

	sSeatReqVt      = vt;
	sSeatReqPending = true;
	sSeatReqDone    = false;

	uint64_t one = 1;
	if (write(sSeatWakeFd, &one, sizeof(one)) < 0) {
		sSeatReqPending = false;
		pthread_mutex_unlock(&sSeatReqLock);
		return B_ERROR;
	}

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 5;

	while (!sSeatReqDone) {
		if (pthread_cond_timedwait(&sSeatReqCond, &sSeatReqLock, &ts) != 0)
			break;
	}

	status_t result = sSeatReqDone ? sSeatReqResult : B_TIMED_OUT;
	sSeatReqPending = false;
	pthread_cond_broadcast(&sSeatReqCond);
	pthread_mutex_unlock(&sSeatReqLock);
	return result;
}


static int
find_app_by_sig(const char* sig)
{
	for (int i = 0; i < sAppCount; i++)
		if (strcmp(sApps[i].signature, sig) == 0)
			return i;
	return -1;
}


static void unregister_app_with_supervisor(const char* signature, pid_t pid);


static void
invalidate_app_by_pid(pid_t pid)
{
	char signature[256] = "";

	pthread_mutex_lock(&sAppsLock);
	for (int i = 0; i < sAppCount; i++) {
		if (sApps[i].pid == pid) {
			strlcpy(signature, sApps[i].signature, sizeof(signature));
			sApps[i].pid  = -1;
			sApps[i].port = -1;
			break;
		}
	}
	pthread_mutex_unlock(&sAppsLock);

	if (signature[0] != '\0')
		unregister_app_with_supervisor(signature, pid);
}


// Fire-and-forget mirror into the supervisor's registry.
static void
register_app_with_supervisor(const char* name, const char* signature,
	pid_t pid, port_id port)
{
	port_id supervisorPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	if (supervisorPort < 0)
		return;

	BPrivate::KMessage msg(BPrivate::B_JANUS_REGISTER_APP);
	msg.AddString("name", name);
	msg.AddString("signature", signature);
	msg.AddInt32("pid", (int32)pid);
	msg.AddInt32("port", (int32)port);
	msg.SendTo(supervisorPort, -1);
}


static void
unregister_app_with_supervisor(const char* signature, pid_t pid)
{
	port_id supervisorPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	if (supervisorPort < 0)
		return;

	BPrivate::KMessage msg(BPrivate::B_JANUS_UNREGISTER_APP);
	msg.AddString("signature", signature);
	msg.AddInt32("pid", (int32)pid);
	msg.SendTo(supervisorPort, -1);
}


static void
send_seat_message(const char* signature, int32 what, bigtime_t timeout)
{
	pthread_mutex_lock(&sAppsLock);
	int idx = find_app_by_sig(signature);
	port_id port = (idx >= 0) ? sApps[idx].port : -1;
	pid_t   pid  = (idx >= 0) ? sApps[idx].pid  : -1;
	pthread_mutex_unlock(&sAppsLock);

	if (port < 0)
		return;

	BMessenger app;
	BMessenger::Private(app).SetTo(pid, port, B_PREFERRED_TOKEN);
	BMessage msg(what);
	if (timeout > 0) {
		BMessage reply;
		app.SendMessage(&msg, &reply, timeout, timeout);
	} else {
		app.SendMessage(&msg);
	}
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

	pthread_mutex_lock(&sAppsLock);
	int idx = find_app_by_sig(signature);
	if (idx >= 0 && sApps[idx].port >= 0) {
		port_id port = sApps[idx].port;
		pid_t   pid  = sApps[idx].pid;
		pthread_mutex_unlock(&sAppsLock);
		BMessage reply(B_OK);
		reply.AddInt32("port", (int32)port);
		reply.AddInt32("team", (int32)pid);
		msg->SendReply(&reply);
		return;
	}
	pthread_mutex_unlock(&sAppsLock);

	BMessage reply(B_NAME_NOT_FOUND);
	msg->SendReply(&reply);
}


static bool
wait_for_server_ready(const char* name, int timeout_ms)
{
	for (int waited = 0; waited < timeout_ms; waited += 50) {
		pthread_mutex_lock(&sAppsLock);
		bool ready = false;
		for (int i = 0; i < sAppCount; i++) {
			if (strcmp(sApps[i].name, name) == 0 && sApps[i].port > 0) {
				ready = true;
				break;
			}
		}
		pthread_mutex_unlock(&sAppsLock);
		if (ready)
			return true;
		usleep(50 * 1000);
	}
	return false;
}


// Fork+exec with privilege drop; this process never runs anything as root.
static void
handle_launch_job(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	// Not a public port: only our own fork()ed children learn its name, so
	// accepting uid 0 here is the actual trust boundary.
	if (sender_uid != 0) {
		fprintf(stderr, "janus_session: B_LAUNCH_JOB rejected from uid=%u\n",
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
	for (int i = 0; kFanOutServers[i].name != NULL; i++) {
		if (strcmp(kFanOutServers[i].name, name) == 0) {
			ks = &kFanOutServers[i];
			break;
		}
	}
	if (ks == NULL) {
		fprintf(stderr, "janus_session: unknown server: %s\n", name);
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
				fprintf(stderr, "janus_session: binary not found: %s\n", name);
				BPrivate::KMessage reply(B_ERROR);
				kmsg.SendReply(&reply);
				return;
			}
		}
	}

	gid_t* groups = NULL;
	int ngroups = 0;
	if (!resolve_user_groups(&groups, &ngroups)) {
		fprintf(stderr, "janus_session: cannot resolve groups for %s\n", sUserName);
		BPrivate::KMessage reply(B_ERROR);
		kmsg.SendReply(&reply);
		return;
	}

	int fds[2];
	if (pipe(fds) != 0) {
		perror("janus_session: pipe");
		free(groups);
		BPrivate::KMessage reply(B_ERROR);
		kmsg.SendReply(&reply);
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);

	pid_t pid = fork();
	if (pid < 0) {
		perror("janus_session: fork");
		close(fds[0]);
		close(fds[1]);
		free(groups);
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

		if (ks->needs_drm && sDrmFd >= 0) {
			int ifd = dup(sDrmFd);
			if (ifd >= 0) {
				snprintf(buf, sizeof(buf), "%d", ifd);
				setenv("JANUS_DRM_FD", buf, 1);
			}
		}

		snprintf(buf, sizeof(buf), "%u", (unsigned)sUserUid);
		setenv("SUDO_UID", buf, 1);
		snprintf(buf, sizeof(buf), "%u", (unsigned)sUserGid);
		setenv("SUDO_GID", buf, 1);

		setenv("XDG_SESSION_TYPE",    "vos",       1);
		setenv("XDG_CURRENT_DESKTOP", "Vitruvian", 1);
		setenv("XDG_SESSION_DESKTOP", "vitruvian", 1);
		setenv("XDG_DATA_DIRS",   "/system/data:/usr/local/share:/usr/share", 0);
		setenv("XDG_CONFIG_DIRS", "/system/settings:/etc/xdg",                0);

		if (sUserHome[0] != '\0') {
			char xdgPath[PATH_MAX + 32];
			snprintf(xdgPath, sizeof(xdgPath), "%s/config/settings", sUserHome);
			setenv("XDG_CONFIG_HOME", xdgPath, 0);
			snprintf(xdgPath, sizeof(xdgPath), "%s/config/data", sUserHome);
			setenv("XDG_DATA_HOME", xdgPath, 0);
			snprintf(xdgPath, sizeof(xdgPath), "%s/config/cache", sUserHome);
			setenv("XDG_CACHE_HOME", xdgPath, 0);
		}

		char runtimeDir[64];
		snprintf(runtimeDir, sizeof(runtimeDir), "/run/user/%u", (unsigned)sUserUid);
		struct stat st;
		if (stat(runtimeDir, &st) == 0 && S_ISDIR(st.st_mode))
			setenv("XDG_RUNTIME_DIR", runtimeDir, 0);
		else {
			fprintf(stderr, "janus_session: %s not usable for %s, "
				"XDG_RUNTIME_DIR left unset (%s)\n", runtimeDir, name, strerror(errno));
		}

		if (sPamEnv != NULL) {
			for (char** e = sPamEnv; *e != NULL; ++e) {
				char* eq = strchr(*e, '=');
				if (eq == NULL)
					continue;
				*eq = '\0';
				setenv(*e, eq + 1, 1);
				*eq = '=';
			}
		}
		setenv("HOME",    sUserHome, 1);
		setenv("USER",    sUserName, 1);
		setenv("LOGNAME", sUserName, 1);
		if (!drop_to_user(groups, ngroups)) {
			fprintf(stderr, "janus_session: drop-priv for %s failed: %s\n",
				name, strerror(errno));
			_exit(126);
		}
		if (chdir(sUserHome) != 0) {
			fprintf(stderr, "janus_session: chdir(%s) for %s: %s\n",
				sUserHome, name, strerror(errno));
		}

		execl(path, name, NULL);
		fprintf(stderr, "janus_session: execl(%s) failed: %s\n", path, strerror(errno));
		_exit(127);
	}

	close(fds[1]);
	free(groups);

	pthread_mutex_lock(&sAppsLock);
	int app_idx = -1;
	for (int i = 0; i < sAppCount; i++) {
		if (sApps[i].pid == -1 && strcmp(sApps[i].signature, ks->signature) == 0) {
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
		fprintf(stderr, "janus_session: sApps[] full\n");
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
			printf("janus_session: launched %s pid=%d, awaiting readiness\n",
				name, (int)pid);
			return;
		}
	}

	fprintf(stderr, "janus_session: sPending[] full for %s\n", name);
	close(fds[0]);
	BPrivate::KMessage reply(B_OK);
	reply.AddInt32("pid", (int32)pid);
	reply.SendTo(replyPort, replyToken);
}


static void
check_pending_launches()
{
	for (int i = 0; i < JANUS_MAX_PENDING; i++) {
		if (!sPending[i].active)
			continue;

		struct pollfd pfd;
		pfd.fd = sPending[i].ready_fd;
		pfd.events = POLLIN;
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
			fprintf(stderr, "janus_session: readiness timeout for %s\n", sApps[idx].name);
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
			printf("janus_session: %s ready, pid=%d port=%d\n",
				sApps[idx].name, (int)sApps[idx].pid, (int)port);

			char pidpath[128];
			snprintf(pidpath, sizeof(pidpath), "%s/%s.pid", runtime_dir(), sApps[idx].name);
			FILE* f = fopen(pidpath, "w");
			if (f != NULL) {
				fprintf(f, "%d\n", (int)sApps[idx].pid);
				fclose(f);
			}

			register_app_with_supervisor(sApps[idx].name, sApps[idx].signature,
				sApps[idx].pid, port);
		}

		BPrivate::KMessage reply(port >= 0 ? B_OK : B_ERROR);
		if (port >= 0)
			reply.AddInt32("pid", (int32)sApps[idx].pid);
		reply.SendTo(sPending[i].reply_port, sPending[i].reply_token);

		sPending[i].active = false;
	}
}


static void
fire_launch(const char* name)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "janus_session: fork(%s): %s\n", name, strerror(errno));
		return;
	}
	if (pid == 0) {
		execl("/system/servers/janus_launch", "janus_launch", name, NULL);
		_exit(127);
	}
}


static void
handle_switch_vt(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	// Authorization already happened at the supervisor; this port trusts
	// uid 0 (the relay), not sUserUid.
	if (sender_uid != 0) {
		fprintf(stderr, "janus_session: B_JANUS_SWITCH_VT rejected from uid=%u\n",
			(unsigned)sender_uid);
		BPrivate::KMessage reply(B_NOT_ALLOWED);
		kmsg.SendReply(&reply);
		return;
	}

	int32 vt = 0;
	if (kmsg.FindInt32("vt", &vt) != B_OK || vt < 1 || vt > 63) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}
	if (sSeat == NULL) {
		BPrivate::KMessage reply(B_NO_INIT);
		kmsg.SendReply(&reply);
		return;
	}

	status_t status = submit_switch_vt((int)vt);
	BPrivate::KMessage reply(status);
	kmsg.SendReply(&reply);
}


static void
launch_port_dispatch()
{
	port_message_info mi = {};
	status_t infoRc = get_port_message_info_etc(sLaunchPort, &mi,
		B_RELATIVE_TIMEOUT, 50000);
	if (infoRc == B_TIMED_OUT || infoRc == B_INTERRUPTED) {
		check_pending_launches();
		return;
	}
	if (infoRc < B_OK)
		return;

	ssize_t bufSize = (ssize_t)mi.size;
	int32 code = 0;
	void* buf = (bufSize > 0) ? malloc(bufSize) : NULL;

	ssize_t r = read_port(sLaunchPort, &code, buf, bufSize > 0 ? bufSize : 0);
	if (r < B_OK) {
		free(buf);
		check_pending_launches();
		return;
	}

	if (code == 'KMSG') {
		BPrivate::KMessage kmsg;
		if (bufSize > 0)
			kmsg.SetTo((const void*)buf, bufSize);
		if (kmsg.What() == BPrivate::B_LAUNCH_JOB) {
			if (!sShuttingDown)
				handle_launch_job(kmsg, mi.sender);
		} else if (kmsg.What() == BPrivate::B_JANUS_SWITCH_VT) {
			if (!sShuttingDown)
				handle_switch_vt(kmsg, mi.sender);
		}
	} else {
		BMessage* msg = new BMessage();
		if (bufSize > 0 && msg->Unflatten((const char*)buf) == B_OK) {
			if (msg->what == BPrivate::B_GET_LAUNCH_DATA)
				handle_get_launch_data(msg);
			else if (msg->IsSourceWaiting()) {
				BMessage reply(B_NOT_SUPPORTED);
				msg->SendReply(&reply);
			}
		}
		delete msg;
	}

	free(buf);
	check_pending_launches();
}


static int32
launch_daemon_thread(void*)
{
	while (sRunning)
		launch_port_dispatch();
	return 0;
}


// --- Seat / DRM -----------------------------------------------------

static void
janus_teardown_seat()
{
	send_seat_message("application/x-vnd.Be-input_server", B_SEAT_DISABLED, 2000000LL);
	send_seat_message("application/x-vnd.Haiku-app_server", B_SEAT_DISABLED, 2000000LL);

	pthread_mutex_lock(&sAppsLock);
	int count = sAppCount;
	pthread_mutex_unlock(&sAppsLock);

	for (int i = 0; i < count; i++) {
		pthread_mutex_lock(&sAppsLock);
		const char* sig  = sApps[i].signature;
		port_id     port = sApps[i].port;
		pid_t       pid  = sApps[i].pid;
		pthread_mutex_unlock(&sAppsLock);

		if (port < 0)
			continue;
		if (strcmp(sig, "application/x-vnd.Be-input_server") == 0
				|| strcmp(sig, "application/x-vnd.Haiku-app_server") == 0)
			continue;

		BMessenger app;
		BMessenger::Private(app).SetTo(pid, port, B_PREFERRED_TOKEN);
		BMessage msg(B_SEAT_DISABLED);
		app.SendMessage(&msg);
	}
}


static void
seat_enable_cb(struct libseat*, void*)
{
	printf("janus_session: seat enabled\n");
	sSessionActive = true;

	pthread_mutex_lock(&sAppsLock);
	int count = sAppCount;
	pthread_mutex_unlock(&sAppsLock);

	for (int i = 0; i < count; i++) {
		pthread_mutex_lock(&sAppsLock);
		port_id port = sApps[i].port;
		pid_t   pid  = sApps[i].pid;
		pthread_mutex_unlock(&sAppsLock);
		if (port < 0)
			continue;
		BMessenger app;
		BMessenger::Private(app).SetTo(pid, port, B_PREFERRED_TOKEN);
		BMessage msg(B_SEAT_ENABLED);
		app.SendMessage(&msg);
	}
}


static void
seat_disable_cb(struct libseat* seat, void*)
{
	printf("janus_session: seat disabled\n");
	sSessionActive = false;
	if (!sShuttingDown)
		janus_teardown_seat();
	libseat_disable_seat(seat);
}


static struct libseat_seat_listener sSeatListener = {
	.enable_seat  = seat_enable_cb,
	.disable_seat = seat_disable_cb,
};


static bool
init_seat()
{
	sSeat = libseat_open_seat(&sSeatListener, NULL);
	if (!sSeat) {
		fprintf(stderr, "janus_session: libseat_open_seat failed\n");
		return false;
	}
	for (int i = 0; i < 10 && !sSessionActive; i++)
		libseat_dispatch(sSeat, 100);
	if (!sSessionActive)
		fprintf(stderr, "janus_session: seat did not activate quickly; continuing\n");
	return true;
}


static bool
open_drm_device()
{
	if (!sSeat)
		return false;
	char path[64];
	for (int i = 0; i <= 9; i++) {
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		sDrmDeviceId = libseat_open_device(sSeat, path, &sDrmFd);
		if (sDrmDeviceId >= 0 && sDrmFd >= 0) {
			printf("janus_session: opened DRM device %s fd=%d\n", path, sDrmFd);
			return true;
		}
	}
	fprintf(stderr, "janus_session: could not open any DRM device\n");
	return false;
}


// --- Fan-out lifecycle --------------------------------------------------

static void
kill_fanout()
{
	pid_t victims[JANUS_MAX_APPS];
	int n = 0;

	pthread_mutex_lock(&sAppsLock);
	for (int i = 0; i < sAppCount; i++) {
		if (sApps[i].pid > 0)
			victims[n++] = sApps[i].pid;
	}
	pthread_mutex_unlock(&sAppsLock);

	for (int i = 0; i < n; i++)
		kill(victims[i], SIGTERM);

	// DRM master isn't released until app_server exits.
	for (int wait_ms = 0; wait_ms < 3000; wait_ms += 20) {
		bool all_gone = true;
		for (int i = 0; i < n; i++) {
			if (victims[i] <= 0)
				continue;
			int status = 0;
			pid_t r = waitpid(victims[i], &status, WNOHANG);
			if (r == victims[i] || (r < 0 && errno == ECHILD))
				victims[i] = 0;
			else
				all_gone = false;
		}
		if (all_gone)
			break;
		usleep(20 * 1000);
	}
	for (int i = 0; i < n; i++) {
		if (victims[i] > 0) {
			fprintf(stderr, "janus_session: pid=%d slow to exit; SIGKILL\n", (int)victims[i]);
			kill(victims[i], SIGKILL);
			waitpid(victims[i], NULL, 0);
		}
	}

	pthread_mutex_lock(&sAppsLock);
	sAppCount = 0;
	pthread_mutex_unlock(&sAppsLock);
}


// Logs on timeout instead of failing; the fan-out proceeds regardless.
static void
fire_and_wait(const char* name)
{
	fire_launch(name);
	if (!wait_for_server_ready(name, 5000))
		fprintf(stderr, "janus_session: %s not ready in 5s\n", name);
}


static void*
fanout_thread(void*)
{
	if (sGreeterMode) {
		const char* frontend =
			access("/var/lib/vos/first-boot-done", F_OK) == 0
				? "vitruvian-login" : "FirstBootPrompt";

		fire_and_wait("registrar");
		fire_and_wait("app_server");
		// Fired explicitly, or app_server self-launches it as an untracked
		// orphan child that survives this session's teardown.
		fire_and_wait("input_server");
		fire_and_wait(frontend);
	} else {
		fire_and_wait("registrar");
		fire_and_wait("app_server");

		// vos-polkit-agent is non-greeter only: it registers itself as the
		// authentication agent for this logind session, and there is no
		// authenticated subject before login.
		static const char* const kFanOut[] = {
			"input_server", "notification_server", "vos-polkit-agent",
			"Deskbar", "Tracker", NULL
		};
		for (int i = 0; kFanOut[i] != NULL; i++)
			fire_launch(kFanOut[i]);
	}
	return NULL;
}


static void
spawn_fanout()
{
	pthread_t th;
	if (pthread_create(&th, NULL, fanout_thread, NULL) != 0) {
		fprintf(stderr, "janus_session: fanout_thread: %s\n", strerror(errno));
		return;
	}
	pthread_detach(th);
}


// --- First login -----------------------------------------------------

static int
first_login_filter(const struct dirent* de)
{
	return de->d_name[0] != '.';
}


static void
run_first_login_for_user()
{
	if (sUserUid == (uid_t)-1 || sUserHome[0] == '\0')
		return;

	char marker[PATH_MAX + 32];
	snprintf(marker, sizeof(marker), "%s/config/settings/first_login", sUserHome);
	if (access(marker, F_OK) == 0)
		return;

	static const char kDir[] = "/system/boot/first_login";
	struct dirent** entries = NULL;
	int n = scandir(kDir, &entries, first_login_filter, alphasort);
	if (n < 0)
		return;

	gid_t* groups = NULL;
	int ngroups = 0;
	if (!resolve_user_groups(&groups, &ngroups)) {
		fprintf(stderr, "janus_session: first_login: cannot resolve groups for %s\n",
			sUserName);
		for (int i = 0; i < n; i++)
			free(entries[i]);
		free(entries);
		return;
	}

	for (int i = 0; i < n; i++) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", kDir, entries[i]->d_name);
		if (access(path, X_OK) != 0)
			continue;

		pid_t child = fork();
		if (child < 0)
			continue;
		if (child == 0) {
			setenv("HOME",    sUserHome, 1);
			setenv("USER",    sUserName, 1);
			setenv("LOGNAME", sUserName, 1);
			if (!drop_to_user(groups, ngroups)) {
				fprintf(stderr, "janus_session: first_login setuid failed: %s\n",
					strerror(errno));
				_exit(1);
			}
			char* const argv[] = { path, NULL };
			execv(path, argv);
			_exit(127);
		}
		int status;
		waitpid(child, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "janus_session: first_login: %s exited %d\n",
				entries[i]->d_name, status);
		}
	}

	for (int i = 0; i < n; i++)
		free(entries[i]);
	free(entries);
	free(groups);

	char m[PATH_MAX + 32];
	snprintf(m, sizeof(m), "%s/config", sUserHome);
	mkdir(m, 0755);
	chown(m, sUserUid, sUserGid);
	snprintf(m, sizeof(m), "%s/config/settings", sUserHome);
	mkdir(m, 0755);
	chown(m, sUserUid, sUserGid);
	snprintf(m, sizeof(m), "%s/config/settings/first_login", sUserHome);
	int fd = open(m, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
	if (fd >= 0) {
		close(fd);
		chown(m, sUserUid, sUserGid);
	}
}


static void*
_first_login_thread(void*)
{
	if (!wait_for_server_ready("Deskbar", 15000)) {
		fprintf(stderr, "janus_session: first_login: Deskbar not ready in 15s; "
			"running anyway (replicants may not install)\n");
	}
	run_first_login_for_user();
	return NULL;
}


static void
run_first_login_for_user_detached()
{
	pthread_t th;
	if (pthread_create(&th, NULL, _first_login_thread, NULL) != 0) {
		fprintf(stderr, "janus_session: first_login pthread_create: %s\n",
			strerror(errno));
		return;
	}
	pthread_detach(th);
}


// --- Teardown / main -----------------------------------------------------

static void
sig_handler(int)
{
	sRunning = false;
}


// cgroup membership, not parentage, pins a logind session open: a
// user-started app reparents to init on logout but stays in this cgroup,
// keeping the session (and user@<uid>.service) alive unless swept here too.
static void
kill_session_cgroup()
{
	char line[PATH_MAX];
	FILE* f = fopen("/proc/self/cgroup", "r");
	if (f == NULL)
		return;
	char relative[PATH_MAX] = "";
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strncmp(line, "0::", 3) != 0)
			continue;
		strlcpy(relative, line + 3, sizeof(relative));
		size_t len = strlen(relative);
		while (len > 0 && (relative[len - 1] == '\n' || relative[len - 1] == '\r'))
			relative[--len] = '\0';
		break;
	}
	fclose(f);
	if (relative[0] != '/')
		return;

	char procsPath[PATH_MAX + 64];
	if (snprintf(procsPath, sizeof(procsPath), "/sys/fs/cgroup%s/cgroup.procs",
			relative) >= (int)sizeof(procsPath)) {
		return;
	}

	pid_t self = getpid();
	for (int round = 0; round < 2; round++) {
		int sig = round == 0 ? SIGTERM : SIGKILL;
		FILE* p = fopen(procsPath, "r");
		if (p == NULL)
			return;
		int killed = 0;
		while (fgets(line, sizeof(line), p) != NULL) {
			pid_t pid = (pid_t)strtol(line, NULL, 10);
			if (pid <= 1 || pid == self)
				continue;
			fprintf(stderr, "janus_session: stray pid=%d in session cgroup;"
				" %s\n", (int)pid, sig == SIGTERM ? "SIGTERM" : "SIGKILL");
			kill(pid, sig);
			killed++;
		}
		fclose(p);
		if (killed == 0)
			return;
		// Strays are not our children; poll the cgroup until init reaps them.
		for (int waited = 0; waited < 2000; waited += 50) {
			usleep(50 * 1000);
			FILE* check = fopen(procsPath, "r");
			if (check == NULL)
				return;
			bool empty = true;
			while (fgets(line, sizeof(line), check) != NULL) {
				pid_t pid = (pid_t)strtol(line, NULL, 10);
				if (pid > 1 && pid != self) {
					empty = false;
					break;
				}
			}
			fclose(check);
			if (empty)
				return;
		}
	}
}


static void
teardown_and_exit()
{
	sShuttingDown = true;
	kill_fanout();
	kill_session_cgroup();

	if (sDrmDeviceId >= 0 && sSeat)
		libseat_close_device(sSeat, sDrmDeviceId);
	if (sSeat) {
		libseat_disable_seat(sSeat);
		libseat_close_seat(sSeat);
		sSeat = NULL;
	}
	close_pam_session();
	_exit(0);
}


static void
process_seat_request()
{
	pthread_mutex_lock(&sSeatReqLock);
	bool pending = sSeatReqPending && !sSeatReqDone;
	int vt = sSeatReqVt;
	pthread_mutex_unlock(&sSeatReqLock);

	if (!pending)
		return;

	status_t result = B_OK;
	if (sSeat == NULL)
		result = B_NO_INIT;
	else if (libseat_switch_session(sSeat, vt) != 0) {
		result = B_ERROR;
		fprintf(stderr, "janus_session: libseat_switch_session(%d) failed\n",
			vt);
	}

	pthread_mutex_lock(&sSeatReqLock);
	sSeatReqResult = result;
	sSeatReqDone   = true;
	pthread_cond_broadcast(&sSeatReqCond);
	pthread_mutex_unlock(&sSeatReqLock);
}


static void
daemon_loop()
{
	int seat_fd = sSeat ? libseat_get_fd(sSeat) : -1;

	sDaemonLoopActive = true;

	while (sRunning) {
		struct pollfd pfds[2];
		int count = 0;
		if (seat_fd >= 0) {
			pfds[count].fd = seat_fd;
			pfds[count].events = POLLIN;
			pfds[count].revents = 0;
			count++;
		}
		if (sSeatWakeFd >= 0) {
			pfds[count].fd = sSeatWakeFd;
			pfds[count].events = POLLIN;
			pfds[count].revents = 0;
			count++;
		}

		if (count == 0)
			usleep(200 * 1000);
		else if (poll(pfds, count, 200) > 0) {
			for (int i = 0; i < count; i++) {
				if ((pfds[i].revents & POLLIN) == 0)
					continue;
				if (pfds[i].fd == seat_fd) {
					if (sSeat)
						libseat_dispatch(sSeat, 0);
				} else {
					uint64_t value;
					(void)read(sSeatWakeFd, &value, sizeof(value));
				}
			}
		}

		process_seat_request();

		pid_t reaped;
		while ((reaped = waitpid(-1, NULL, WNOHANG)) > 0)
			invalidate_app_by_pid(reaped);
	}

	pthread_mutex_lock(&sSeatReqLock);
	sDaemonLoopActive = false;
	sSeatReqDone = true;
	pthread_cond_broadcast(&sSeatReqCond);
	pthread_mutex_unlock(&sSeatReqLock);
}


int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	setsid();
	signal(SIGTERM, sig_handler);
	signal(SIGINT,  SIG_IGN);
	signal(SIGCHLD, SIG_DFL);

	if (!resolve_identity())
		return 1;

	char title[128];
	snprintf(title, sizeof(title), "janus_session (%s)", sUserName);
	set_proc_title(argc, argv, title);

	runtime_dir();

	if (!init_pam_session()) {
		fprintf(stderr, "janus_session: PAM open failed for %s; exiting\n", sUserName);
		return 1;
	}

	sSeatWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (sSeatWakeFd < 0)
		fprintf(stderr, "janus_session: eventfd: %s\n", strerror(errno));

	if (!init_seat())
		fprintf(stderr, "janus_session: running without seat session\n");
	else
		open_drm_device();

	// A unique local port per instance, not the supervisor's well-known name.
	snprintf(sLaunchPortName, sizeof(sLaunchPortName),
		"vos:janus_session_launch.%d", (int)getpid());
	setenv("JANUS_LAUNCH_PORT_NAME", sLaunchPortName, 1);

	sLaunchPort = create_port(50, sLaunchPortName);
	if (sLaunchPort < 0) {
		fprintf(stderr, "janus_session: create_port failed: %s\n", strerror(errno));
		teardown_and_exit();
	}

	{
		thread_id t = spawn_thread(launch_daemon_thread, "janus_session:launch",
			B_NORMAL_PRIORITY, NULL);
		if (t < 0) {
			fprintf(stderr, "janus_session: spawn_thread failed\n");
			teardown_and_exit();
		}
		resume_thread(t);
	}

	{
		port_id supervisorPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
		if (supervisorPort >= 0) {
			BPrivate::KMessage hello(BPrivate::B_JANUS_SESSION_HELLO);
			hello.AddString("port", sLaunchPortName);
			hello.AddInt32("uid", (int32)sUserUid);
			hello.AddBool("greeter", sGreeterMode);
			hello.SendTo(supervisorPort, -1);
		} else {
			fprintf(stderr, "janus_session: supervisor port not found at startup\n");
		}
	}

	spawn_fanout();
	if (!sGreeterMode)
		run_first_login_for_user_detached();

	daemon_loop();

	teardown_and_exit();
	return 0; // unreachable
}
