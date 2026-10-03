/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Backend interface LocationProvider drives; only kit sources implement it.
 */
#ifndef LOCATION_SOURCE_H
#define LOCATION_SOURCE_H

#include <Messenger.h>
#include <String.h>
#include <SupportDefs.h>


// Snapshot messages a source posts to the provider's messenger. Payload
// fields match the public B_LOCATION_* watchers; accuracy snapping happens
// in LocationProvider before it publishes.
static const uint32 kMsgLocationData = 'LcDt';
static const uint32 kMsgLocationStatus = 'LcSs';


class LocationSource {
public:
	virtual					~LocationSource() {}

			// Deliver snapshots to target until Stop(). Idempotent.
			virtual	status_t	Start(BMessenger target) = 0;
			virtual	void		Stop() = 0;
			virtual	const char*	Name() const = 0;
};


#endif	// LOCATION_SOURCE_H
