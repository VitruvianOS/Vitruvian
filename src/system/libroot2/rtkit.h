/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _RTKIT_H
#define _RTKIT_H


#include <sys/types.h>


namespace BKernelPrivate {


// Client for org.freedesktop.RealtimeKit1, loaded lazily through dlopen() so
// libroot carries no link dependency. All calls return 0 or a negative errno
// and remember a missing service so the bus is only probed once.
int RtkitMakeRealtime(pid_t tid, int priority, int* granted);
int RtkitMakeHighPriority(pid_t tid, int nice, int* granted);

// Soft limit for RLIMIT_RTTIME rtkit demands, in microseconds.
long long RtkitRTTimeMax();


}


#endif
