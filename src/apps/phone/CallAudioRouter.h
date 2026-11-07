/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CALL_AUDIO_ROUTER_H
#define _CALL_AUDIO_ROUTER_H


#include <String.h>


// Call audio through ALSA UCM: the card's verb switches to "Voice Call" for
// the call and back to "HiFi" afterwards, one device per route. Applied via
// alsaucm(1), so there is no libasound link and no-hardware is a no-op.
class CallAudioRouter {
public:
	static	CallAudioRouter*	Default();

			status_t			BeginCall();
			status_t			EndCall();

			// ROUTE_HANDSET: Earpiece + mic; ROUTE_SPEAKER: Speaker
			// + mic; ROUTE_HEADSET: Headphone + headset mic.
			status_t			SetRoute(uint32 route);
			uint32				Route() const { return fRoute; }

			status_t			SetMuted(bool muted);
			bool				IsMuted() const { return fMuted; }

	enum {
		ROUTE_HANDSET = 0,
		ROUTE_SPEAKER = 1,
		ROUTE_HEADSET = 2
	};

private:
								CallAudioRouter();

			status_t			_FindCard();
			status_t			_Run(const char* verb, const char* device);
			status_t			_EnableDevices(const char* first,
									const char* second);

			BString				fCard;
			uint32				fRoute;
			bool				fMuted;
			bool				fInCall;
};


#endif // _CALL_AUDIO_ROUTER_H
