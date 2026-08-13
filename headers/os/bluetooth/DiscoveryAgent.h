/*
 * Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 *
 * Reimplemented on top of BlueZBackend instead of raw HCI
 * inquiry commands.
 * Divergences from haiku-latest/headers/os/bluetooth/DiscoveryAgent.h:
 *  - No `accessCode` (GIAC/LIAC): that selects an HCI inquiry access code,
 *    which BlueZ's StartDiscovery has no equivalent knob for -- it always
 *    does a general inquiry.
 *  - RetrieveDevices()/CancelInquiry(int) are gone; CancelInquiry(listener)
 *    is kept since it maps directly onto StopDiscovery.
 *  - StartInquiry() is fire-and-return like every other kit call reaching
 *    BlueZBackend; it does not itself return the scan result, only whether
 *    the request was queued -- results arrive at the listener.
 */
#ifndef _DISCOVERY_AGENT_H
#define _DISCOVERY_AGENT_H

#include <String.h>
#include <SupportDefs.h>

#include <bluetooth/DiscoveryListener.h>

class BMessageRunner;

namespace Bluetooth {

// Default/bounds for the inquiry window, seconds -- same numeric range
// upstream enforced (BT_MIN/MAX_INQUIRY_TIME), just expressed directly in
// seconds instead of 1.28s HCI inquiry ticks, which have no BlueZ meaning.
#define BT_DEFAULT_INQUIRY_TIME_SECS	10
#define BT_MIN_INQUIRY_TIME_SECS		1
#define BT_MAX_INQUIRY_TIME_SECS		61

class DiscoveryAgent {

public:
	explicit DiscoveryAgent(const BString& adapterPath);
	~DiscoveryAgent();

	// Starts BlueZ discovery on this agent's adapter. NOTIFICATION_DEVICE_FOUND
	// is routed to `listener` for `secs` seconds, after which discovery is
	// stopped automatically and listener->InquiryCompleted(INQUIRY_COMPLETED)
	// fires. BlueZBackend supports any number of concurrent watchers, each
	// with its own mask, so a scan's DEVICE_FOUND registration coexists with
	// other property-change watchers (e.g. the preflet's own StartWatching).
	status_t StartInquiry(DiscoveryListener* listener,
		bigtime_t secs = BT_DEFAULT_INQUIRY_TIME_SECS * 1000000LL);
	status_t CancelInquiry(DiscoveryListener* listener);

private:
	BString fAdapterPath;
	DiscoveryListener* fListener;
	BMessageRunner* fTimeoutRunner;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryAgent;
#endif

#endif // _DISCOVERY_AGENT_H
