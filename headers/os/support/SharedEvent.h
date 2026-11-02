/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OS_SUPPORT_SHARED_EVENT_H
#define _OS_SUPPORT_SHARED_EVENT_H


#include <OS.h>
#include <SupportDefs.h>


/* Counting event on a shared word, placed as for BSharedLocker. Wait()
 * consumes one Post(). */
struct shared_event_state {
	int32	fCount;
	int32	_reserved[3];
};


class BSharedEvent {
public:
							BSharedEvent();
							BSharedEvent(shared_event_state* state);
	virtual					~BSharedEvent();

			void				SetTo(shared_event_state* state);

			void				Post();
			status_t			Wait();
			status_t			WaitWithTimeout(bigtime_t timeout);

			void				Reset();

private:
							BSharedEvent(const BSharedEvent&);
			BSharedEvent&		operator=(const BSharedEvent&);

			shared_event_state*	fState;
};


#endif	// _OS_SUPPORT_SHARED_EVENT_H
