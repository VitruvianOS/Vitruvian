/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <OS.h>
#include <kernel/util/KMessage.h>

#include <LaunchDaemonDefs.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: janus_launch <server-name>\n");
		return 1;
	}

	const char* name = argv[1];

	port_id launchPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	if (launchPort < 0) {
		fprintf(stderr, "janus_launch: launch daemon port not found"
			" (is janus running?)\n");
		return 1;
	}

	BPrivate::KMessage request(BPrivate::B_LAUNCH_JOB);
	request.AddString("name", name);

	BPrivate::KMessage reply;
	// Send timeout 5 s, reply timeout 15 s (janus waits up to 10 s for readiness)
	status_t err = request.SendTo(launchPort, -1, &reply,
		5000000LL, 15000000LL, getpid());

	if (err != B_OK) {
		fprintf(stderr, "janus_launch: SendTo failed for %s: %s\n",
			name, strerror(-err));
		return 1;
	}

	if (reply.What() != B_OK) {
		fprintf(stderr, "janus_launch: janus returned error for %s\n", name);
		return 1;
	}

	int32 pid = -1;
	reply.FindInt32("pid", &pid);
	if (pid <= 0) {
		fprintf(stderr, "janus_launch: no pid in reply for %s\n", name);
		return 1;
	}

	// Write PID file — systemd reads this for Type=forking tracking
	mkdir("/run/vitruvian", 0755);
	char pidpath[128];
	snprintf(pidpath, sizeof(pidpath), "/run/vitruvian/%s.pid", name);
	FILE* f = fopen(pidpath, "w");
	if (f == NULL) {
		fprintf(stderr, "janus_launch: could not write %s: %s\n",
			pidpath, strerror(errno));
		return 1;
	}
	fprintf(f, "%d\n", (int)pid);
	fclose(f);

	printf("janus_launch: %s started, pid=%d\n", name, (int)pid);
	return 0;
}
