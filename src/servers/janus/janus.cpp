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
	  false, false },
	{ "app_server",
	  "application/x-vnd.Haiku-app_server",
	  "picasso",
	  true,  false },
	{ "input_server",
	  "application/x-vnd.Be-input_server",
	  NULL,
	  false, true  },
	{ "mount_server",
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
	{ NULL, NULL, NULL, false, false }
};


// Desktop user resolved once from $VITRUVIAN_DEFAULT_USER (default vitruvio).
static char  sUserName[64]        = "";
static char  sUserHome[PATH_MAX]  = "";
static uid_t sUserUid             = (uid_t)-1;
static gid_t sUserGid             = (gid_t)-1;


static bool
resolve_user()
{
	const char* name = getenv("VITRUVIAN_DEFAULT_USER");
	if (name == NULL || *name == '\0')
		name = "vitruvio";

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
	static struct pam_conv conv = { pam_null_conv, NULL };
	int r = pam_start("vitruvian-session", sUserName, &conv, &sPamHandle);
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
static void
run_first_login_for_user()
{
	if (sUserUid == (uid_t)-1 || sUserHome[0] == '\0')
		return;

	char marker[PATH_MAX];
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
		if (initgroups(sUserName, sUserGid) != 0
				|| setgid(sUserGid) != 0
				|| setuid(sUserUid) != 0) {
			fprintf(stderr, "janus: first_login setuid failed: %s\n",
				strerror(errno));
			_exit(1);
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

		// Write the marker last so a failed script doesn't gate future
		// retries.
		char m[PATH_MAX];
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

static struct libseat* sSeat        = NULL;
static int             sDrmFd       = -1;
static int             sDrmDeviceId = -1;
static volatile bool   sSessionActive = false;
static volatile bool   sRunning       = true;
static volatile bool   sShuttingDown  = false;

static port_id sLaunchPort = -1;

#define JANUS_MAX_POWER_FDS 8
static int sPowerFds[JANUS_MAX_POWER_FDS];
static int sPowerFdCount = 0;

static void janus_handle_shutdown(bool reboot);


static void
send_reg_shut_down(bool reboot)
{
	port_id regPort = find_port(B_REGISTRAR_PORT_NAME);
	if (regPort < 0) {
		fprintf(stderr, "janus: cannot find registrar port\n");
		return;
	}
	BMessage msg(BPrivate::B_REG_SHUT_DOWN);
	msg.AddBool("reboot", reboot);
	msg.AddBool("confirm", false);
	ssize_t size = msg.FlattenedSize();
	char* buf = new char[size];
	if (msg.Flatten(buf, size) == B_OK)
		write_port(regPort, 0, buf, size);
	delete[] buf;
}


static void
open_power_devices()
{
	DIR* dir = opendir("/dev/input");
	if (dir == NULL)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL && sPowerFdCount < JANUS_MAX_POWER_FDS) {
		if (strncmp(entry->d_name, "event", 5) != 0)
			continue;

		char path[64];
		snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;

		// Accept dedicated power/sleep button devices:
		// must have EV_KEY + KEY_POWER or KEY_SLEEP, must NOT have EV_REL or EV_ABS
		// (excludes keyboards and mice that incidentally report power keys).
		unsigned long evBits[EV_MAX / (8 * sizeof(unsigned long)) + 1] = {};
		ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits);

		auto hasBit = [](unsigned long* bits, int bit) -> bool {
			return (bits[bit / (8 * sizeof(unsigned long))]
				>> (bit % (8 * sizeof(unsigned long)))) & 1UL;
		};

		if (!hasBit(evBits, EV_KEY) || hasBit(evBits, EV_REL) || hasBit(evBits, EV_ABS)) {
			close(fd);
			continue;
		}

		unsigned long keyBits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = {};
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);

		if (!hasBit(keyBits, KEY_POWER) && !hasBit(keyBits, KEY_SLEEP)
				&& !hasBit(keyBits, KEY_RESTART)) {
			close(fd);
			continue;
		}

		char name[128] = {};
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		printf("janus: power device: %s (%s)\n", path, name);
		sPowerFds[sPowerFdCount++] = fd;
	}
	closedir(dir);
}


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

	// Fallback
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
		snprintf(path, sizeof(path), "/system/%s", name);
		if (access(path, X_OK) != 0) {
			fprintf(stderr, "janus: server not found: %s\n", name);
			BPrivate::KMessage reply(B_ERROR);
			kmsg.SendReply(&reply);
			return;
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

		if (sUserUid != (uid_t)-1) {
			snprintf(buf, sizeof(buf), "%u", (unsigned)sUserUid);
			setenv("SUDO_UID", buf, 1);
			snprintf(buf, sizeof(buf), "%u", (unsigned)sUserGid);
			setenv("SUDO_GID", buf, 1);
		}

		if (ks->run_as_user) {
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

	// Parent: close write end — only child holds it now
	close(fds[1]);

	// Register in sApps[]
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

	// Extract reply info from KMessage delivery header before losing scope
	port_id replyPort  = kmsg.ReplyPort();
	int32   replyToken = kmsg.ReplyToken();

	// Find a free sPending[] slot — reply is deferred until pipe fires
	for (int i = 0; i < JANUS_MAX_PENDING; i++) {
		if (!sPending[i].active) {
			sPending[i].active      = true;
			sPending[i].app_idx     = app_idx;
			sPending[i].ready_fd    = fds[0];
			sPending[i].reply_port  = replyPort;
			sPending[i].reply_token = replyToken;
			sPending[i].deadline    = system_time() + 10000000LL; // 10 s
			printf("janus: launched %s pid=%d, awaiting readiness\n",
				name, (int)pid);
			return;
		}
	}

	// sPending[] full — reply immediately with pid, port stays -1
	fprintf(stderr, "janus: sPending[] full for %s, replying without port\n", name);
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
		pfd.fd      = sPending[i].ready_fd;
		pfd.events  = POLLIN;
		pfd.revents = 0;
		int ret = poll(&pfd, 1, 0);

		port_id port = -1;
		bool resolved = false;

		if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			ssize_t n = read(sPending[i].ready_fd, &port, sizeof(port));
			resolved = true;
			// check wether child died
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

			// Write PID file for systemd (Type=forking + PIDFile=)
			char pidpath[128];
			snprintf(pidpath, sizeof(pidpath), "/run/vitruvian/%s.pid",
				sApps[idx].name);
			FILE* f = fopen(pidpath, "w");
			if (f != NULL) {
				fprintf(f, "%d\n", (int)sApps[idx].pid);
				fclose(f);
			} else {
				fprintf(stderr, "janus: could not write %s: %s\n",
					pidpath, strerror(errno));
			}
		}

		// Send deferred reply to janus_launch via KMessage
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
	// SIGTERM = systemd shutdown request; treat as clean exit so the
	// daemon_loop drops its libseat/port refs (and closes any held
	// /dev/nexus fds) before systemd starts umounting rootfs.
	signal(SIGTERM, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGCHLD, SIG_DFL);

	mkdir("/run/vitruvian", 0755);

	if (!resolve_user()) {
		fprintf(stderr, "janus: desktop user not found "
			"(check VITRUVIAN_DEFAULT_USER)\n");
	} else if (init_pam_session()) {
		run_first_login_for_user();
	}

	if (!init_seat())
		fprintf(stderr, "janus: running without seat session\n");
	else
		open_drm_device();

	if (!init_launch_daemon_port())
		return 1;

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
