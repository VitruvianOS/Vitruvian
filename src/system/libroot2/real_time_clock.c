/*
 * Copyright 2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <Debug.h>

#include <time.h>
#include <stdint.h>


void
set_real_time_clock(uint64_t currentTime)
{
	struct timespec ts;
	ts.tv_sec = (time_t)currentTime;
	ts.tv_nsec = 0;
	if (clock_settime(CLOCK_REALTIME, &ts) != 0)
		debugger("set_real_time_clock: can't set clock");
}


uint64_t
real_time_clock()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec 
		+ (uint64_t)ts.tv_nsec / 1000000000;
}


bigtime_t
real_time_clock_usecs()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return -1;
	return (bigtime_t)ts.tv_sec * 1000000LL + (bigtime_t)(ts.tv_nsec / 1000);
}


uint64_t
real_time_clock_ns()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
