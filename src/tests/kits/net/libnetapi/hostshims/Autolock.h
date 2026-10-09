/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_AUTOLOCK_H
#define _HOSTSHIMS_AUTOLOCK_H

#include <Locker.h>

class BAutolock {
public:
	BAutolock(BLocker& locker) : fLocker(locker) { fLocker.Lock(); }
	BAutolock(BLocker* locker) : fLocker(*locker) { fLocker.Lock(); }
	~BAutolock() { fLocker.Unlock(); }

private:
	BLocker& fLocker;
};

#endif
