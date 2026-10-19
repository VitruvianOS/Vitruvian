/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DISPLAY_CHANGE_TRACKER_H
#define _DISPLAY_CHANGE_TRACKER_H


#include <stdint.h>


/*!	State machine behind the "Keep these settings?" confirmation.
	app_server owns the deadline so an unconfirmed display change
	reverts even when the preflet died; the logic is plain C++ so a
	host test can cover it without the window server.
*/
class DisplayChangeTracker {
public:
								DisplayChangeTracker();

			bool				IsPending() const { return fPending; }

			// -1 when no change is pending.
			int32_t				Owner() const { return fPending ? fOwner : -1; }

			// Timer threads carry the generation they were started for,
			// so a restart makes their stale timeout a no-op.
			uint32_t			Generation() const { return fGeneration; }

			// Opens a window, or joins one that is already pending: the
			// first snapshot and owner stand for the whole transaction.
			// Returns the generation the deadline timer must carry.
			uint32_t			Begin(int32_t owner);

			// The three ways a window resolves. True means the caller
			// must act: persist for Confirm, revert for the others.
			bool				Confirm(int32_t owner);
			bool				Cancel(int32_t owner);
			bool				Timeout(uint32_t generation);
			bool				OwnerDied(int32_t owner);

private:
			bool				fPending;
			int32_t				fOwner;
			uint32_t			fGeneration;
};


#endif // _DISPLAY_CHANGE_TRACKER_H
