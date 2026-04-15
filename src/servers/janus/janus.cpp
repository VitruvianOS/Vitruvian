/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libseat.h>
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


// ---------------------------------------------------------------------------
// VT switch message constants (received by input_server and app_server)
// ---------------------------------------------------------------------------

#define B_SEAT_DISABLED  'sdis'
#define B_SEAT_ENABLED   'senb'


// ---------------------------------------------------------------------------
// Known servers table
// ---------------------------------------------------------------------------

struct KnownServer {
	const char* name;
	const char* signature;
	const char* port_name;  // non-NULL = find_port() fallback for B_GET_LAUNCH_DATA
	bool        needs_drm;
};

static const KnownServer kKnownServers[] = {
	{ "registrar",
	  B_REGISTRAR_SIGNATURE,
	  B_REGISTRAR_PORT_NAME,
	  false },
	{ "app_server",
	  "application/x-vnd.Haiku-app_server",
	  "picasso",
	  true  },
	{ "input_server",
	  "application/x-vnd.Be-input_server",
	  NULL,
	  false },
	{ "mount_server",
	  "application/x-vnd.Haiku-mount_server",
	  NULL,
	  false },
	{ "notification_server",
	  "application/x-vnd.Haiku-notification_server",
	  NULL,
	  false },
	{ "Deskbar",
	  "application/x-vnd.Be-TSKB",
	  NULL,
	  false },
	{ "Tracker",
	  "application/x-vnd.Be-TRAK",
	  NULL,
	  false },
	{ NULL, NULL, NULL, false }
};


// ---------------------------------------------------------------------------
// Live app table — populated by handle_launch_job when port is resolved
// ---------------------------------------------------------------------------

struct JanusApp {
	char    name[64];
	char    signature[256];
	pid_t   pid;
	port_id port;  // -1 until readiness pipe delivers the port_id
};

#define JANUS_MAX_APPS 32
static JanusApp        sApps[JANUS_MAX_APPS];
static int             sAppCount = 0;
static pthread_mutex_t sAppsLock = PTHREAD_MUTEX_INITIALIZER;


// ---------------------------------------------------------------------------
// Pending launch — one slot per in-flight B_LAUNCH_JOB
// ---------------------------------------------------------------------------

struct PendingLaunch {
	bool      active;
	int       app_idx;     // index into sApps[]
	int       ready_fd;    // read end of readiness pipe
	port_id   reply_port;  // extracted from KMessage delivery info
	int32     reply_token;
	bigtime_t deadline;    // system_time() + 10s
};

#define JANUS_MAX_PENDING 8
static PendingLaunch sPending[JANUS_MAX_PENDING];


// ---------------------------------------------------------------------------
// Seat / DRM state
// ---------------------------------------------------------------------------

static struct libseat* sSeat        = NULL;
static int             sDrmFd       = -1;
static int             sDrmDeviceId = -1;
static volatile bool   sSessionActive = false;
static volatile bool   sRunning       = true;

static port_id sLaunchPort = -1;


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

	// 1. Live sApps[] table (ports resolved via readiness pipe)
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

	// 2. Static fallback via well-known port name (registrar, app_server)
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
handle_launch_job(BPrivate::KMessage& kmsg)
{
	const char* name = NULL;
	if (kmsg.FindString("name", &name) != B_OK || name == NULL) {
		BPrivate::KMessage reply(B_BAD_VALUE);
		kmsg.SendReply(&reply);
		return;
	}

	// Look up in known servers
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

	// Resolve binary path — servers live in /system/servers/, apps in /system/
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

	// Readiness pipe: child writes fMsgPort after BApplication registration
	int fds[2];
	if (pipe(fds) != 0) {
		perror("janus: pipe");
		BPrivate::KMessage reply(B_ERROR);
		kmsg.SendReply(&reply);
		return;
	}
	// Read end is ours — set CLOEXEC so it's not inherited by child
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	// Write end (fds[1]) has no CLOEXEC → survives exec in child

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
		// Child process
		close(fds[0]);

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
		int ret = poll(&pfd, 1, 0);  // non-blocking

		port_id port = -1;
		bool resolved = false;

		if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			ssize_t n = read(sPending[i].ready_fd, &port, sizeof(port));
			resolved = true;
			if (n != (ssize_t)sizeof(port))
				port = -1;  // child died without writing
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
		ssize_t bufSize = port_buffer_size_etc(sLaunchPort,
			B_RELATIVE_TIMEOUT, 50000 /* 50 ms */);

		if (bufSize == B_TIMED_OUT || bufSize == B_INTERRUPTED) {
			check_pending_launches();
			continue;
		}
		if (bufSize < B_OK)
			break;

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
			if (kmsg.What() == BPrivate::B_LAUNCH_JOB)
				handle_launch_job(kmsg);
		} else {
			// All other messages (B_GET_LAUNCH_DATA etc.) use BMessage (libbe)
			BMessage* msg = new BMessage();
			if (bufSize > 0 && msg->Unflatten((const char*)buf) == B_OK) {
				switch (msg->what) {
					case BPrivate::B_GET_LAUNCH_DATA:
						handle_get_launch_data(msg);
						break;
					default:
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
seat_disable_cb(struct libseat* seat, void* /*data*/)
{
	printf("janus: seat disabled\n");
	sSessionActive = false;

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

		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
	}
}


int
main(int /*argc*/, char** /*argv*/)
{
	signal(SIGTERM, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGCHLD, SIG_DFL);

	// Ensure /run/vitruvian/ exists for PID files
	mkdir("/run/vitruvian", 0755);

	if (!init_seat())
		fprintf(stderr, "janus: running without seat session\n");
	else
		open_drm_device();

	if (!init_launch_daemon_port())
		return 1;

	daemon_loop();

	// Cleanup
	sRunning = false;
	if (sLaunchPort >= 0) {
		delete_port(sLaunchPort);
		sLaunchPort = -1;
	}
	if (sDrmDeviceId >= 0 && sSeat)
		libseat_close_device(sSeat, sDrmDeviceId);
	if (sSeat)
		libseat_close_seat(sSeat);

	return 0;
}
