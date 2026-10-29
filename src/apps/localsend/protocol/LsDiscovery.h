/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LS_DISCOVERY_H
#define _LS_DISCOVERY_H

#include <pthread.h>
#include <stdint.h>

#include <string>

#include "LsDevice.h"

namespace LocalSend {

// Multicast UDP discovery per protocol section 3.1. The socket code is
// plain POSIX so the same implementation host-tests over loopback.

class LsDiscoveryListener {
public:
	virtual					~LsDiscoveryListener() {}

	// Called for every valid announce packet from another device.
	// |sourceAddress| is the sender's IPv4 address, needed for the
	// HTTP register response.
	virtual	void			DeviceAnnounced(const LsDevice& device,
									const std::string& sourceAddress,
									bool announceFlag) = 0;
};


class LsDiscovery {
public:
							LsDiscovery();
							~LsDiscovery();

			// 0 on success, -1 on failure (errno-style detail in the
			// log; callers only need success/failure).
			int				Init(uint16_t port = 53317,
								const std::string& group = "224.0.0.167");

			// Starts the receive thread. The listener is called on
			// that thread; it must be set before Start and stay
			// valid until Stop returns.
			int				Start(LsDiscoveryListener* listener);
			void			Stop();

			// Sends one announce (or a response with announce=false)
			// to the multicast group.
			int				Announce(const LsDevice& device,
								bool announceFlag = true);

			// Parses a datagram payload. Exposed for unit tests.
	static	bool			ParsePacket(const char* data, size_t length,
								LsAnnounceMessage& message);

			bool			IsRunning() const { return fRunning; }

private:
			static	void*	_ThreadEntry(void* data);
			void			_ReadLoop();

			int				fSocket;
			uint16_t		fPort;
			std::string		fGroup;
			pthread_t		fThread;
			volatile bool	fRunning;
			bool			fThreadStarted;
			LsDiscoveryListener* fListener;
};

} // namespace LocalSend

#endif // _LS_DISCOVERY_H
