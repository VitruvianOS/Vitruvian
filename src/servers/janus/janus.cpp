/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libseat.h>
#include <security/pam_appl.h>
#include <systemd/sd-bus.h>
}

#include <AppDefs.h>
#include <List.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>

#include <kernel/util/KMessage.h>

#include <LaunchDaemonDefs.h>
#include <MessengerPrivate.h>
#include <RegistrarDefs.h>
#include <syscalls.h>


struct KnownServer {
	const char* name;
	const char* signature;
	const char* port_name;
	bool        needs_drm;
	bool        run_as_user;
};

static const KnownServer kKnownServers[] = {
	{ "registrar",
	  B_REGISTRAR_SIGNATURE,
	  B_REGISTRAR_PORT_NAME,
	  false, true  },
	{ "app_server",
	  "application/x-vnd.Haiku-app_server",
	  "picasso",
	  true,  true  },
	{ "input_server",
	  "application/x-vnd.Be-input_server",
	  NULL,
	  false, true  },
	{ "mount_server",  // root: mount(2) needs CAP_SYS_ADMIN
	  "application/x-vnd.Haiku-mount_server",
	  NULL,
	  false, false },
	{ "notification_server",
	  "application/x-vnd.Haiku-notification_server",
	  NULL,
	  false, true  },
	{ "Deskbar",
	  "application/x-vnd.Be-TSKB",
	  NULL,
	  false, true  },
	{ "Tracker",
	  "application/x-vnd.Be-TRAK",
	  NULL,
	  false, true  },
	{ "vitruvian-login",
	  "application/x-vnd.Vitruvian-login",
	  NULL,
	  false, true  },
	{ "FirstBootPrompt",
	  "application/x-vnd.Haiku-FirstBootPrompt",
	  NULL,
	  false, true  },
	{ NULL, NULL, NULL, false, false }
};


static char  sUserName[64]        = "";
static char  sUserHome[PATH_MAX]  = "";
static uid_t sUserUid             = (uid_t)-1;
static gid_t sUserGid             = (gid_t)-1;
static bool  sSystemMode          = false;
static char  sRuntimeDir[PATH_MAX]= "";
static bool is_graphical_login_allowed(struct passwd* pw);
static bool  sGreeterMode         = false;


static const char*
runtime_dir()
{
	if (sRuntimeDir[0] != '\0')
		return sRuntimeDir;

	const char* xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg != NULL && *xdg != '\0') {
		snprintf(sRuntimeDir, sizeof(sRuntimeDir), "%s/vos", xdg);
	} else {
		snprintf(sRuntimeDir, sizeof(sRuntimeDir), "/run/vos");
	}
	mkdir(sRuntimeDir, 0755);
	return sRuntimeDir;
}


static bool
resolve_user()
{
	if (getuid() != 0) {
		struct passwd* pw = getpwuid(getuid());
		if (pw == NULL)
			return false;
		strncpy(sUserName, pw->pw_name, sizeof(sUserName) - 1);
		strncpy(sUserHome, pw->pw_dir,  sizeof(sUserHome) - 1);
		sUserUid = pw->pw_uid;
		sUserGid = pw->pw_gid;
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

	const char* name;
	if (autoName[0] != '\0'
			&& is_graphical_login_allowed(getpwnam(autoName))) {
		name = autoName;
		sGreeterMode = false;
	} else if (sGreeterMode) {
		name = "vos_login";
	} else {
		name = getenv("VOS_DEFAULT_USER");
		if (name == NULL || *name == '\0')
			name = "vos-live";
	}

	struct passwd* pw = getpwnam(name);
	if (pw == NULL)
		return false;

	strncpy(sUserName, pw->pw_name, sizeof(sUserName) - 1);
	strncpy(sUserHome, pw->pw_dir,  sizeof(sUserHome) - 1);
	sUserUid = pw->pw_uid;
	sUserGid = pw->pw_gid;
	return true;
}


// PAM session for the desktop user — runs pam_env / pam_limits / pam_umask
// / pam_systemd so session-stage forks inherit XDG_RUNTIME_DIR etc.

static pam_handle_t* sPamHandle = NULL;
static char**        sPamEnv    = NULL;


static int
pam_null_conv(int num, const struct pam_message** /*msg*/,
	struct pam_response** resp, void* /*data*/)
{
	*resp = (struct pam_response*)calloc(num, sizeof(struct pam_response));
	return *resp != NULL ? PAM_SUCCESS : PAM_BUF_ERR;
}


static bool
init_pam_session()
{
	if (!sSystemMode) {
		// pam_systemd from the login shell already owns the session.
		printf("janus: user-mode, inheriting PAM environment from login\n");
		return true;
	}

	static struct pam_conv conv = { pam_null_conv, NULL };
	// class=greeter stops systemd from starting pipewire/dbus/etc. for vos_login.
	const char* stack = sGreeterMode ? "vitruvian-greeter" : "vitruvian-session";
	int r = pam_start(stack, sUserName, &conv, &sPamHandle);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus: pam_start failed (%d)\n", r);
		sPamHandle = NULL;
		return false;
	}
	pam_set_item(sPamHandle, PAM_TTY,   "tty1");
	pam_set_item(sPamHandle, PAM_RUSER, "root");

	r = pam_setcred(sPamHandle, PAM_ESTABLISH_CRED);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus: pam_setcred failed: %s\n",
			pam_strerror(sPamHandle, r));
		pam_end(sPamHandle, r);
		sPamHandle = NULL;
		return false;
	}

	r = pam_open_session(sPamHandle, 0);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "janus: pam_open_session failed: %s\n",
			pam_strerror(sPamHandle, r));
		pam_setcred(sPamHandle, PAM_DELETE_CRED);
		pam_end(sPamHandle, r);
		sPamHandle = NULL;
		return false;
	}

	sPamEnv = pam_getenvlist(sPamHandle);
	printf("janus: PAM session opened for %s\n", sUserName);
	return true;
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


// scandir(3) filter: skip . / .. / hidden.
static int
first_login_filter(const struct dirent* de)
{
	return de->d_name[0] != '.';
}


// Runs /system/boot/first_login/* once per user, gated by
// ~/config/settings/first_login. Alphasort like run-parts(8).
// Blocking — callers wanting to keep the main dispatcher live should
// use run_first_login_for_user_detached() below.
static void
run_first_login_for_user()
{
	if (sUserUid == (uid_t)-1 || sUserHome[0] == '\0')
		return;

	char marker[PATH_MAX + 32];
	snprintf(marker, sizeof(marker), "%s/config/settings/first_login",
		sUserHome);
	if (access(marker, F_OK) == 0)
		return;

	static const char kDir[] = "/system/boot/first_login";
	struct dirent** entries = NULL;
	int n = scandir(kDir, &entries, first_login_filter, alphasort);
	if (n < 0)
		return;

	pid_t pid = fork();
	if (pid < 0) {
		for (int i = 0; i < n; i++)
			free(entries[i]);
		free(entries);
		fprintf(stderr, "janus: first_login fork: %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		// User-mode: already the target user; setuid would EPERM.
		if (sSystemMode) {
			if (initgroups(sUserName, sUserGid) != 0
					|| setgid(sUserGid) != 0
					|| setuid(sUserUid) != 0) {
				fprintf(stderr, "janus: first_login setuid failed: %s\n",
					strerror(errno));
				_exit(1);
			}
		}
		setenv("HOME",    sUserHome, 1);
		setenv("USER",    sUserName, 1);
		setenv("LOGNAME", sUserName, 1);

		for (int i = 0; i < n; i++) {
			char path[PATH_MAX];
			snprintf(path, sizeof(path), "%s/%s", kDir, entries[i]->d_name);
			if (access(path, X_OK) != 0)
				continue;

			pid_t child = fork();
			if (child < 0)
				continue;
			if (child == 0) {
				char* const argv[] = { path, NULL };
				execv(path, argv);
				_exit(127);
			}
			int status;
			waitpid(child, &status, 0);
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
				fprintf(stderr, "janus: first_login: %s exited %d\n",
					entries[i]->d_name, status);
			}
		}

		// Marker last so a failed script doesn't gate the next retry.
		char m[PATH_MAX + 32];
		snprintf(m, sizeof(m), "%s/config/settings", sUserHome);
		mkdir(m, 0755);
		snprintf(m, sizeof(m), "%s/config/settings/first_login", sUserHome);
		int fd = open(m, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
		if (fd >= 0)
			close(fd);
		_exit(0);
	}

	for (int i = 0; i < n; i++)
		free(entries[i]);
	free(entries);

	int status;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "janus: first_login runner exited %d\n", status);
	}
}


// Detached so the IPC dispatcher doesn't block on script I/O.
static void*
_first_login_thread(void*)
{
	run_first_login_for_user();
	return NULL;
}


static void
run_first_login_for_user_detached()
{
	pthread_t th;
	if (pthread_create(&th, NULL, _first_login_thread, NULL) != 0) {
		fprintf(stderr, "janus: first_login pthread_create: %s\n",
			strerror(errno));
		return;
	}
	pthread_detach(th);
}


struct JanusApp {
	char    name[64];
	char    signature[256];
	pid_t   pid;
	port_id port;
};

#define JANUS_MAX_APPS 32
static JanusApp        sApps[JANUS_MAX_APPS];
static int             sAppCount = 0;
static pthread_mutex_t sAppsLock = PTHREAD_MUTEX_INITIALIZER;


struct PendingLaunch {
	bool      active;
	int       app_idx;
	int       ready_fd;
	port_id   reply_port;
	int32     reply_token;
	bigtime_t deadline;
};

#define JANUS_MAX_PENDING 8
static PendingLaunch sPending[JANUS_MAX_PENDING];

static const bigtime_t kLaunchReadinessTimeoutUsec = 10 * 1000000LL;

static struct libseat* sSeat        = NULL;
static int             sDrmFd       = -1;
static int             sDrmDeviceId = -1;
static volatile bool   sSessionActive = false;
static volatile bool   sRunning       = true;
static volatile bool   sShuttingDown  = false;

static port_id sLaunchPort = -1;

static void janus_handle_shutdown(bool reboot);


static int
find_app_by_sig(const char* sig)
{
	for (int i = 0; i < sAppCount; i++)
		if (strcmp(sApps[i].signature, sig) == 0)
			return i;
	return -1;
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
handle_launch_job(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	// System-mode: only accept from root. User-mode: only accept from
	// our own uid (i.e. from own-session processes).
	uid_t allowed = sSystemMode ? (uid_t)0 : sUserUid;
	if (sender_uid != allowed) {
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

		if (ks->needs_drm && sDrmFd >= 0) {
			// libseat opens with O_CLOEXEC — dup to get inheritable fd
			int ifd = dup(sDrmFd);
			if (ifd >= 0) {
				snprintf(buf, sizeof(buf), "%d", ifd);
				setenv("JANUS_DRM_FD", buf, 1);
			}
		}

		// LibEvdevEventStream (hardcoded US scancode table) is dormant
		// by default — input_server runs pre-auth and drives xkbcommon
		// from /etc/vconsole.conf. Set VITRUVIAN_FALLBACK_INPUT=1 in
		// the environment before spawning janus if you want the embed
		// path back (emergency / no-libinput scenarios).
		if (sGreeterMode && strcmp(name, "app_server") == 0
				&& getenv("VITRUVIAN_FALLBACK_INPUT") != NULL)
			setenv("APP_SERVER_EMBED_INPUT", "1", 1);

		if (sUserUid != (uid_t)-1) {
			snprintf(buf, sizeof(buf), "%u", (unsigned)sUserUid);
			setenv("SUDO_UID", buf, 1);
			snprintf(buf, sizeof(buf), "%u", (unsigned)sUserGid);
			setenv("SUDO_GID", buf, 1);
		}

		// SESSION_TYPE=vos is Vitruvian-native; x11/wayland-only toolkits
		// fall back to their default backend.
		setenv("XDG_SESSION_TYPE",    "vos",        1);
		setenv("XDG_CURRENT_DESKTOP", "Vitruvian",  1);
		setenv("XDG_SESSION_DESKTOP", "vitruvian",  1);
		setenv("XDG_DATA_DIRS",   "/system/data:/usr/local/share:/usr/share", 0);
		setenv("XDG_CONFIG_DIRS", "/system/settings:/etc/xdg",                0);

		// User-mode: already correct uid; initgroups would need CAP_SETGID.
		if (ks->run_as_user && sSystemMode) {
			if (sUserUid == (uid_t)-1) {
				fprintf(stderr, "janus: %s wants run_as_user but user "
					"not resolved\n", name);
				_exit(125);
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
			if (initgroups(sUserName, sUserGid) != 0
					|| setgid(sUserGid) != 0
					|| setuid(sUserUid) != 0) {
				fprintf(stderr, "janus: drop-priv for %s failed: %s\n",
					name, strerror(errno));
				_exit(126);
			}
			if (chdir(sUserHome) != 0) {
				fprintf(stderr, "janus: chdir(%s) for %s: %s\n",
					sUserHome, name, strerror(errno));
			}
		}

		execl(path, name, NULL);
		fprintf(stderr, "janus: execl(%s) failed: %s\n", path, strerror(errno));
		_exit(127);
	}

	close(fds[1]);

	pthread_mutex_lock(&sAppsLock);
	int app_idx = -1;
	if (sAppCount < JANUS_MAX_APPS) {
		app_idx = sAppCount++;
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

	// Deferred reply — the child writes its port on the pipe.
	for (int i = 0; i < JANUS_MAX_PENDING; i++) {
		if (!sPending[i].active) {
			sPending[i].active      = true;
			sPending[i].app_idx     = app_idx;
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
		// Only ECHO_OFF gets the password. ECHO_ON is used for username /
		// second-factor prompts — never leak the password into those.
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


// Binds AUTH and LOGIN_OK: one-shot, cleared on consume or on failed auth.
static char      sAuthenticatedUser[64] = "";
static bigtime_t sAuthenticatedAt = 0;
static const bigtime_t kAuthWindowUsec = 30 * 1000000LL;	// 30s


// vos-live: allowed only when /etc/vos/live exists AND sentinel doesn't.
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


// Sets vos-live's password to "live" and starts sshd. Caller must
// already have checked /etc/vos/debug exists.
static void
enable_ssh_debug()
{
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("sh", "sh", "-c",
			"echo 'vos-live:live' | chpasswd 2>/dev/null; "
			"systemctl start ssh.service 2>/dev/null || "
			"systemctl start sshd.service 2>/dev/null",
			(char*)NULL);
		_exit(127);
	}
	if (pid > 0) {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}
	fprintf(stderr, "janus: debug SSH enabled (vos-live/live)\n");
}


// root/system accounts are refused; sudo is the only path to uid=0.
static bool
is_graphical_login_allowed(struct passwd* pw)
{
	if (pw == NULL)
		return false;
	if (pw->pw_uid == 0)
		return false;
	// vos_login is a system sysuser used only by the pre-auth chain.
	if (strcmp(pw->pw_name, "vos_login") == 0)
		return false;
	// Nologin / false shells: not real interactive accounts.
	if (pw->pw_shell != NULL
			&& (strstr(pw->pw_shell, "nologin") != NULL
				|| strstr(pw->pw_shell, "/false") != NULL))
		return false;
	// vos-live: dual-gate.
	if (strcmp(pw->pw_name, "vos-live") == 0)
		return is_live_persona_allowed();
	// All other users: require a real uid.
	if (pw->pw_uid < 1000)
		return false;
	return true;
}


// Runs the /etc/pam.d/vitruvian-auth stack.
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

	// Attribution for faillock, audit, and lastlog. PAM_TTY must be a
	// real device name (not ":0" — that's a DISPLAY string), or
	// pam_faillock keys every failure across every seat into a single
	// bucket. tty1 matches the seat0 VT janus's greeter runs on.
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


static bool
_is_pre_auth_server(const char* name)
{
	return strcmp(name, "vitruvian-login") == 0
		|| strcmp(name, "FirstBootPrompt") == 0
		|| strcmp(name, "app_server") == 0
		|| strcmp(name, "registrar") == 0;
}


// Copy a single settings file from vos_login's config into a target user's
// config, creating parent dirs and chowning. Silently no-ops if the source
// doesn't exist (FBP may not have touched every knob).
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
	close(src);
	close(dst);
	if (chown(dstPath, uid, gid) < 0) { /* best effort */ }
}


// Ferry FBP-picked language/keymap from vos_login's settings dir to the
// authenticated user's config so the desktop session boots in the right locale.
static void
seed_user_settings_from_preauth(const char* userHome, uid_t uid, gid_t gid)
{
	if (userHome == NULL || userHome[0] == '\0')
		return;

	// Ensure ~/config exists (chowned) so the settings dir lands correctly.
	char configDir[PATH_MAX];
	char settingsDir[PATH_MAX];
	snprintf(configDir,   sizeof(configDir),   "%s/config", userHome);
	snprintf(settingsDir, sizeof(settingsDir), "%s/config/settings", userHome);

	if (mkdir(configDir, 0755) < 0 && errno != EEXIST) return;
	if (chown(configDir, uid, gid) < 0) { /* best effort */ }

	static const char* kSrcDir = "/system/login/config/settings";
	static const char* kFiles[] = { "Locale settings", "Key_map", NULL };
	for (int i = 0; kFiles[i] != NULL; i++) {
		char src[PATH_MAX];
		snprintf(src, sizeof(src), "%s/%s", kSrcDir, kFiles[i]);
		copy_settings_file(src, settingsDir, kFiles[i], uid, gid);
	}
}


static void
kill_pre_auth_chain()
{
	pid_t victims[JANUS_MAX_APPS];
	int   n = 0;

	pthread_mutex_lock(&sAppsLock);
	for (int i = 0; i < sAppCount; i++) {
		bool preauth = _is_pre_auth_server(sApps[i].name);
		if (preauth && sApps[i].pid > 0)
			victims[n++] = sApps[i].pid;
	}
	pthread_mutex_unlock(&sAppsLock);

	for (int i = 0; i < n; i++)
		kill(victims[i], SIGTERM);

	// Reap first: DRM master isn't released until app_server exits.
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
			fprintf(stderr, "janus: pre-auth pid=%d slow to exit; SIGKILL\n",
				(int)victims[i]);
			kill(victims[i], SIGKILL);
			waitpid(victims[i], NULL, 0);
		}
	}

	pthread_mutex_lock(&sAppsLock);
	int keep = 0;
	for (int i = 0; i < sAppCount; i++) {
		bool preauth = _is_pre_auth_server(sApps[i].name);
		if (preauth)
			continue;
		if (i != keep)
			sApps[keep] = sApps[i];
		keep++;
	}
	sAppCount = keep;
	pthread_mutex_unlock(&sAppsLock);
}


static void
fire_janus_launch(const char* name)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "janus: post-auth fork(%s): %s\n",
			name, strerror(errno));
		return;
	}
	if (pid == 0) {
		execl("/system/servers/janus_launch",
			"janus_launch", name, NULL);
		_exit(127);
	}
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


static void*
post_auth_thread(void* /*arg*/)
{
	fire_janus_launch("registrar");
	if (!wait_for_server_ready("registrar", 5000))
		fprintf(stderr, "janus: post-auth registrar not ready in 5s\n");

	fire_janus_launch("app_server");
	if (!wait_for_server_ready("app_server", 5000))
		fprintf(stderr, "janus: post-auth app_server not ready in 5s\n");

	static const char* const kFanOut[] = {
		"input_server", "mount_server", "notification_server",
		"Deskbar", "Tracker", NULL
	};
	for (int i = 0; kFanOut[i] != NULL; i++)
		fire_janus_launch(kFanOut[i]);
	return NULL;
}


static void
spawn_post_auth_chain()
{
	pthread_t th;
	if (pthread_create(&th, NULL, post_auth_thread, NULL) != 0) {
		fprintf(stderr, "janus: post_auth_thread: %s\n", strerror(errno));
		return;
	}
	pthread_detach(th);
}


// Set by logind_watch_thread on PrepareForShutdown(true). Read by
// handle_logout to decline mid-shutdown logout requests. Plain bool is
// safe: single writer (watch thread), single reader (daemon loop),
// monotonic transition (false -> true), no re-entry needed.
static volatile bool sShutdownInFlight = false;


static void*
logind_watch_thread(void* /*arg*/)
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return NULL;

	// Match PrepareForShutdown on org.freedesktop.login1.Manager.
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
kill_post_auth_chain()
{
	pid_t victims[JANUS_MAX_APPS];
	int   n = 0;

	pthread_mutex_lock(&sAppsLock);
	for (int i = 0; i < sAppCount; i++) {
		bool preauth = _is_pre_auth_server(sApps[i].name);
		if (!preauth && sApps[i].pid > 0)
			victims[n++] = sApps[i].pid;
	}
	pthread_mutex_unlock(&sAppsLock);

	for (int i = 0; i < n; i++)
		kill(victims[i], SIGTERM);

	for (int wait_ms = 0; wait_ms < 1500; wait_ms += 20) {
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
			kill(victims[i], SIGKILL);
			waitpid(victims[i], NULL, 0);
		}
	}

	// Purge post-auth entries before kill_pre_auth_chain does its own —
	// stale port_ids would otherwise survive across relogin.
	pthread_mutex_lock(&sAppsLock);
	int keep = 0;
	for (int i = 0; i < sAppCount; i++) {
		if (_is_pre_auth_server(sApps[i].name)) {
			if (i != keep)
				sApps[keep] = sApps[i];
			keep++;
		}
	}
	sAppCount = keep;
	pthread_mutex_unlock(&sAppsLock);

	kill_pre_auth_chain();
}


static void*
pre_auth_thread(void* /*arg*/)
{
	// Sentinel present → greeter. Absent → FirstBootPrompt.
	const char* frontend =
		access("/var/lib/vos/first-boot-done", F_OK) == 0
			? "vitruvian-login"
			: "FirstBootPrompt";

	fire_janus_launch("registrar");
	if (!wait_for_server_ready("registrar", 5000))
		fprintf(stderr, "janus: pre-auth registrar not ready in 5s\n");

	fire_janus_launch("app_server");
	if (!wait_for_server_ready("app_server", 5000))
		fprintf(stderr, "janus: pre-auth app_server not ready in 5s\n");

	fire_janus_launch(frontend);
	if (!wait_for_server_ready(frontend, 5000))
		fprintf(stderr, "janus: pre-auth %s not ready in 5s\n", frontend);
	return NULL;
}


// MUST be on a separate thread — during logout the caller is the
// launch_daemon dispatcher, and blocking it deadlocks the readiness
// signals from the servers we're about to spawn.
static void
spawn_pre_auth_chain()
{
	pthread_t th;
	if (pthread_create(&th, NULL, pre_auth_thread, NULL) != 0) {
		fprintf(stderr, "janus: pre_auth_thread: %s\n", strerror(errno));
		return;
	}
	pthread_detach(th);
}


static void
handle_logout(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	printf("janus: B_JANUS_LOGOUT received from uid=%u (session uid=%u)\n",
		(unsigned)sender_uid, (unsigned)sUserUid);

	struct passwd* vl = getpwnam("vos_login");
	uid_t voslogin = (vl != NULL) ? vl->pw_uid : (uid_t)-1;
	if (sUserUid == (uid_t)-1 || sUserUid == voslogin
			|| sender_uid != sUserUid) {
		fprintf(stderr, "janus: B_JANUS_LOGOUT rejected sender_uid=%u "
			"(session uid=%u)\n",
			(unsigned)sender_uid, (unsigned)sUserUid);
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

	printf("janus: LOGOUT — tearing down session for %s (uid=%u)\n",
		sUserName, (unsigned)sUserUid);

	BPrivate::KMessage reply(B_OK);
	kmsg.SendReply(&reply);

	close_pam_session();
	kill_post_auth_chain();

	sUserUid = voslogin;
	sUserGid = (vl != NULL) ? vl->pw_gid : (gid_t)-1;
	strncpy(sUserName, "vos_login", sizeof(sUserName) - 1);
	sUserName[sizeof(sUserName) - 1] = '\0';
	if (vl != NULL) {
		strncpy(sUserHome, vl->pw_dir, sizeof(sUserHome) - 1);
		sUserHome[sizeof(sUserHome) - 1] = '\0';
	}
	sGreeterMode = true;

	if (!init_pam_session())
		fprintf(stderr, "janus: greeter PAM open failed; continuing\n");
	spawn_pre_auth_chain();
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

	// mode=live + user=vos-live skips the password ladder; anything
	// else must have a prior verified AUTH_REQUEST.
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
		int32 enableSsh = 0;
		if (kmsg.FindInt32("enable_ssh", &enableSsh) == B_OK && enableSsh
				&& access("/etc/vos/debug", F_OK) == 0) {
			enable_ssh_debug();
		}
	} else {
		// Must pair with a prior AUTH_REQUEST for the same user.
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

	// Reply BEFORE SIGTERM so the greeter can quit cleanly.
	BPrivate::KMessage reply(B_OK);
	kmsg.SendReply(&reply);

	close_pam_session();
	kill_pre_auth_chain();

	strncpy(sUserName, pw->pw_name, sizeof(sUserName) - 1);
	sUserName[sizeof(sUserName) - 1] = '\0';
	strncpy(sUserHome, pw->pw_dir,  sizeof(sUserHome) - 1);
	sUserHome[sizeof(sUserHome) - 1] = '\0';
	sUserUid = pw->pw_uid;
	sUserGid = pw->pw_gid;

	// Without this the respawned app_server keeps APP_SERVER_EMBED_INPUT=1
	// and fights input_server over /dev/input/event*.
	sGreeterMode = false;

	seed_user_settings_from_preauth(sUserHome, sUserUid, sUserGid);

	if (!init_pam_session())
		fprintf(stderr, "janus: post-auth PAM open failed; continuing\n");
	spawn_post_auth_chain();
	run_first_login_for_user_detached();
}


static void
handle_auth_request(BPrivate::KMessage& kmsg, uid_t sender_uid)
{
	// Only vos_login may ask janus to verify credentials.
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

	// `pass` points into kmsg's internal storage; scrub after PAM.
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
			sApps[idx].port = port;
			pthread_mutex_unlock(&sAppsLock);

			printf("janus: %s ready, pid=%d port=%d\n",
				sApps[idx].name, (int)sApps[idx].pid, (int)port);

			// systemd Type=forking + PIDFile= expects this on disk.
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
			// KMessage always arrives with port code 'KMSG'; dispatch by what.
			// Cast to const void* to select SetTo(const void*, int32, uint32 flags),
			// which implies KMESSAGE_INIT_FROM_BUFFER | KMESSAGE_READ_ONLY.
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
			} else if (kmsg.What() == BPrivate::B_JANUS_LOGOUT) {
				if (!sShuttingDown)
					handle_logout(kmsg, mi.sender);
			}
		} else {
			// All other messages (B_GET_LAUNCH_DATA etc.) use BMessage (libbe)
			BMessage* msg = new BMessage();
			if (bufSize > 0 && msg->Unflatten((const char*)buf) == B_OK) {
				switch (msg->what) {
					case BPrivate::B_GET_LAUNCH_DATA:
						handle_get_launch_data(msg);
						break;
					case BPrivate::B_REG_SHUTDOWN_FINISHED: {
						bool reboot = false;
						msg->FindBool("reboot", &reboot);
						delete msg;
						msg = NULL;
						free(buf);
						buf = NULL;
						janus_handle_shutdown(reboot);
						break;
					}
					default:
						// Reply immediately so callers never hang waiting
						// for unimplemented BLaunchRoster messages.
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


static void
seat_enable_cb(struct libseat* /*seat*/, void* /*data*/)
{
	printf("janus: seat enabled\n");
	sSessionActive = true;

	// Notify all launched apps (async — no ordering required for re-enable)
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
janus_teardown_seat()
{
	// Sync: input_server must release grabs; app_server must release DRM master.
	send_seat_message("application/x-vnd.Be-input_server",
		B_SEAT_DISABLED, 2000000LL);
	send_seat_message("application/x-vnd.Haiku-app_server",
		B_SEAT_DISABLED, 2000000LL);

	// Async: notify remaining apps (best-effort)
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
janus_handle_shutdown(bool reboot)
{
	fprintf(stderr, "janus: shutdown handover received, reboot=%d\n", reboot);

	// Set sShuttingDown BEFORE _kern_shutdown so the poll-loop reaper
	// stops stealing the systemctl child's waitpid status.
	sShuttingDown = true;

	sync();

	status_t status = _kern_shutdown(reboot);
	if (status != B_OK) {
		fprintf(stderr, "janus: _kern_shutdown failed (0x%x); "
			"returning to main loop so systemd can retry\n", status);
		return;
	}

	// systemd is now driving shutdown. Release our resources so it can
	// finish cleanly; do not _exit(1) — Restart=on-failure would respawn.
	janus_teardown_seat();

	if (sSeat != NULL) {
		libseat_disable_seat(sSeat);
		libseat_close_seat(sSeat);
		sSeat = NULL;
	}

	if (sDrmFd >= 0) {
		close(sDrmFd);
		sDrmFd = -1;
	}

	close_pam_session();
}


static void
seat_disable_cb(struct libseat* seat, void* /*data*/)
{
	printf("janus: seat disabled\n");
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
		fprintf(stderr, "janus: libseat_open_seat failed\n");
		return false;
	}

	for (int i = 0; i < 10 && !sSessionActive; i++)
		libseat_dispatch(sSeat, 100);

	if (!sSessionActive)
		fprintf(stderr, "janus: seat did not activate quickly; continuing\n");

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
			printf("janus: opened DRM device %s fd=%d\n", path, sDrmFd);
			return true;
		}
	}
	fprintf(stderr, "janus: could not open any DRM device\n");
	return false;
}


static void
sig_handler(int /*sig*/)
{
	sRunning = false;
}


static void
daemon_loop()
{
	int seat_fd = sSeat ? libseat_get_fd(sSeat) : -1;

	while (sRunning) {
		if (seat_fd < 0) {
			sleep(1);
			continue;
		}

		struct pollfd pfd;
		pfd.fd      = seat_fd;
		pfd.events  = POLLIN;
		pfd.revents = 0;

		int ret = poll(&pfd, 1, 1000);
		if (ret > 0 && sSeat)
			libseat_dispatch(sSeat, 0);

		// Stop reaping once shutdown starts: _kern_shutdown forks systemctl
		// and needs its own waitpid to succeed.
		while (!sShuttingDown && waitpid(-1, NULL, WNOHANG) > 0)
			;
	}
}


int
main(int /*argc*/, char** /*argv*/)
{
	// stdout/stderr are redirected to /var/log/janus.log; unbuffer so
	// progress lines land in the log immediately, not once the 4 KiB
	// stdio buffer fills. A frozen log with the process still running
	// otherwise looks like a real hang.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	// SIGTERM = systemd shutdown request; treat as clean exit so the
	// daemon_loop drops its libseat/port refs (and closes any held
	// /dev/nexus fds) before systemd starts umounting rootfs.
	signal(SIGTERM, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGCHLD, SIG_DFL);

	runtime_dir();

	if (access("/var/lib/vos/greeter-enabled", F_OK) == 0)
		sGreeterMode = true;

	if (!resolve_user()) {
		fprintf(stderr, "janus: desktop user not resolved "
			"(user-mode: getpwuid failed; system-mode: check "
			"VOS_DEFAULT_USER)\n");
	} else if (init_pam_session()) {
		run_first_login_for_user();
	}

	if (!init_seat())
		fprintf(stderr, "janus: running without seat session\n");
	else
		open_drm_device();

	if (!init_launch_daemon_port())
		return 1;

	// Start logind PrepareForShutdown watcher (race guard for logout).
	{
		pthread_t th;
		if (pthread_create(&th, NULL, logind_watch_thread, NULL) == 0)
			pthread_detach(th);
		else
			fprintf(stderr, "janus: logind_watch_thread: %s\n",
				strerror(errno));
	}

	if (sSystemMode) {
		if (sGreeterMode) {
			printf("janus: greeter mode enabled — spawning pre-auth chain\n");
			spawn_pre_auth_chain();
		} else {
			printf("janus: autologin as %s — spawning post-auth chain\n",
				sUserName);
			spawn_post_auth_chain();
		}
	}

	daemon_loop();

	sRunning = false;
	if (sLaunchPort >= 0) {
		delete_port(sLaunchPort);
		sLaunchPort = -1;
	}
	if (sDrmDeviceId >= 0 && sSeat)
		libseat_close_device(sSeat, sDrmDeviceId);
	if (sSeat)
		libseat_close_seat(sSeat);

	close_pam_session();
	return 0;
}
