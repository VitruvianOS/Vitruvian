/*
 * Copyright 2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <time.h>
#include <stdint.h>


void
set_real_time_clock(uint64 currentTime) {
	struct timespec ts;
	ts.tv_sec = (time_t)currentTime;
	ts.tv_nsec = 0;

	clock_settime(CLOCK_REALTIME, &ts);
}


uint64_t
real_time_clock() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	return (uint64_t)ts.tv_sec 
		+ (uint64_t)ts.tv_nsec / 1000000000;
}


bigtime_t
real_time_clock_usecs() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	return (bigtime_t)ts.tv_sec * 1000000
		+ (bigtime_t)ts.tv_nsec / 1000;
}
