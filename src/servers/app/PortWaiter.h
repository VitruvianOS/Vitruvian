/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PORT_WAITER_H
#define PORT_WAITER_H


#include <OS.h>


// Waits on a port and an optional absolute deadline with no polling.
class PortWaiter {
public:
	enum result {
		kPortReadable,
		kDeadline,
		kFdReadable,	// an added fd, see LastTag()
		kError			// includes a deleted port
	};

								PortWaiter();
								~PortWaiter();

			// false: no fd, callers read the port blocking
			bool				SetPort(port_id port);
			bool				IsValid() const { return fEpoll >= 0; }
			status_t			AddFd(int fd, int tag);

	// deadline is a system_time() value, or B_INFINITE_TIMEOUT.
			result				Wait(bigtime_t deadline);
			int					LastTag() const { return fLastTag; }

private:
			void				_Disarm();

			int					fEpoll;
			int					fPortFd;
			int					fTimer;
			bool				fArmed;
			int					fLastTag;
};


#endif	// PORT_WAITER_H
