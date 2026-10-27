/*
 * Copyright 2018-2026, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;


// Ask systemd-timedated through timedatectl; refusal maps to B_NOT_ALLOWED.
status_t
set_timezone(const char* timezone)
{
	if (timezone == NULL || timezone[0] == '\0')
		return B_BAD_VALUE;

	char* const argv[] = { (char*)"timedatectl", (char*)"set-timezone",
		(char*)timezone, NULL };

	pid_t child;
	if (posix_spawnp(&child, "timedatectl", NULL, NULL, argv, environ) != 0)
		return B_NOT_ALLOWED;

	int status = 0;
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			return B_ERROR;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return B_NOT_ALLOWED;

	return B_OK;
}


// Returns the time left on the previous alarm, 0 if none was pending.
bigtime_t
set_alarm(bigtime_t when, uint32 mode)
{
	struct itimerval value;
	memset(&value, 0, sizeof(value));

	if (when != B_INFINITE_TIMEOUT) {
		bigtime_t first = when;
		bigtime_t interval = 0;

		switch (mode) {
			case B_PERIODIC_ALARM:
				interval = when;
				break;
			case B_ONE_SHOT_ABSOLUTE_ALARM:
				first = when - system_time();
				break;
			case B_ONE_SHOT_RELATIVE_ALARM:
			default:
				break;
		}

		// a zero it_value would disarm the timer
		if (first < 1)
			first = 1;

		value.it_value.tv_sec = first / 1000000;
		value.it_value.tv_usec = first % 1000000;
		value.it_interval.tv_sec = interval / 1000000;
		value.it_interval.tv_usec = interval % 1000000;
	}

	struct itimerval old;
	if (setitimer(ITIMER_REAL, &value, &old) != 0)
		return 0;

	return (bigtime_t)old.it_value.tv_sec * 1000000 + old.it_value.tv_usec;
}
