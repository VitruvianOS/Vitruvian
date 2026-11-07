/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CallAudioRouter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Autolock.h>
#include <Locker.h>


CallAudioRouter*
CallAudioRouter::Default()
{
	// Deliberately leaked; routes are process-global.
	static CallAudioRouter* instance = new CallAudioRouter();
	return instance;
}


CallAudioRouter::CallAudioRouter()
	:
	fRoute(ROUTE_HANDSET),
	fMuted(false),
	fInCall(false)
{
}


status_t
CallAudioRouter::_FindCard()
{
	if (!fCard.IsEmpty())
		return B_OK;

	// Ask each ALSA card which verbs it exposes; the call-capable one
	// lists "Voice Call". Cards with no UCM config just error out.
	for (int32 card = 0; card < 8; card++) {
		char command[160];
		snprintf(command, sizeof(command),
			"alsaucm -c %d get _verbs 2>/dev/null", (int)card);
		FILE* pipe = popen(command, "r");
		if (pipe == NULL)
			continue;

		char line[256];
		bool found = false;
		while (fgets(line, sizeof(line), pipe) != NULL) {
			if (strstr(line, "Voice Call") != NULL) {
				found = true;
				break;
			}
		}
		pclose(pipe);

		if (found) {
			char name[16];
			snprintf(name, sizeof(name), "%d", (int)card);
			fCard = name;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
CallAudioRouter::_Run(const char* verb, const char* device)
{
	if (fCard.IsEmpty())
		return B_NO_INIT;

	char command[256];
	if (device != NULL) {
		snprintf(command, sizeof(command),
			"alsaucm -c %s set _verb '%s' set _enable '%s' "
			"set _enadev '%s' >/dev/null 2>&1",
			fCard.String(), verb, device, device);
	} else {
		snprintf(command, sizeof(command),
			"alsaucm -c %s set _verb '%s' >/dev/null 2>&1",
			fCard.String(), verb);
	}

	int32 result = system(command);
	return result == 0 ? B_OK : B_ERROR;
}


status_t
CallAudioRouter::_EnableDevices(const char* first, const char* second)
{
	status_t status = _Run("Voice Call", first);
	if (second != NULL) {
		char command[256];
		snprintf(command, sizeof(command),
			"alsaucm -c %s set _enable '%s' set _enadev '%s' "
			">/dev/null 2>&1", fCard.String(), second, second);
		if (system(command) != 0 && status == B_OK)
			status = B_ERROR;
	}
	return status;
}


status_t
CallAudioRouter::BeginCall()
{
	if (_FindCard() != B_OK)
		return B_ENTRY_NOT_FOUND;

	fInCall = true;
	fMuted = false;
	return SetRoute(fRoute);
}


status_t
CallAudioRouter::EndCall()
{
	fInCall = false;
	fMuted = false;
	if (fCard.IsEmpty())
		return B_OK;

	// Back to the media verb so music apps own the card again.
	return _Run("HiFi", NULL);
}


status_t
CallAudioRouter::SetRoute(uint32 route)
{
	fRoute = route;
	if (!fInCall)
		return B_OK;

	switch (route) {
		case ROUTE_SPEAKER:
			return _EnableDevices("Speaker", "DigitalMic");
		case ROUTE_HEADSET:
		{
			status_t status = _EnableDevices("Headphone",
				"HeadsetMic");
			// The A64 PinePhone UCM spells the pair differently.
			if (status != B_OK)
				return _EnableDevices("Headphones", "Headset");
			return status;
		}
		case ROUTE_HANDSET:
		default:
		{
			status_t status = _EnableDevices("Earpiece", "DigitalMic");
			if (status != B_OK)
				return _EnableDevices("Earpiece", "Mic");
			return status;
		}
	}
}


status_t
CallAudioRouter::SetMuted(bool muted)
{
	fMuted = muted;
	if (!fInCall || fCard.IsEmpty())
		return B_OK;

	// Muting is dropping the capture devices out of the verb; unmuting
	// re-enables the route's mic.
	if (!muted)
		return SetRoute(fRoute);

	char command[256];
	snprintf(command, sizeof(command),
		"alsaucm -c %s set _disdev DigitalMic "
		"set _disdev Mic set _disdev HeadsetMic "
		">/dev/null 2>&1", fCard.String());
	return system(command) == 0 ? B_OK : B_ERROR;
}
