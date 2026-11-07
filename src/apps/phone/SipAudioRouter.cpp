/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SipAudioRouter.h"

#include <Autolock.h>

#include <media2/MediaRecorder.h>
#include <media2/SoundPlayer.h>

#include <telephony/TelephonyRoster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// 8 kHz mono signed 16-bit, the G.711 rate the SIP backend speaks.
static const uint32 kAudioRate = 8000;
static const size_t kRingFrames = kAudioRate / 2;	// 500 ms


SipAudioRouter*
SipAudioRouter::Default()
{
	// Deliberately leaked; routes are process-global.
	static SipAudioRouter* instance = new SipAudioRouter();
	return instance;
}


SipAudioRouter::SipAudioRouter()
	:
	fPlayer(NULL),
	fRecorder(NULL),
	fPlayRing(NULL),
	fPlayRead(0),
	fPlayWrite(0),
	fPlaySize(kRingFrames),
	fRecRing(NULL),
	fRecRead(0),
	fRecWrite(0),
	fRecSize(kRingFrames)
{
	fPlayRing = new int16[fPlaySize];
	fRecRing = new int16[fRecSize];
}


SipAudioRouter::~SipAudioRouter()
{
	EndCall();
	delete[] fPlayRing;
	delete[] fRecRing;
}


void
SipAudioRouter::_PlayBuffer(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	SipAudioRouter* router = (SipAudioRouter*)cookie;
	size_t frames = size / sizeof(int16);

	BAutolock lock(router->fLock);
	int16* out = (int16*)buffer;
	for (size_t i = 0; i < frames; i++) {
		if (router->fPlayRead != router->fPlayWrite) {
			out[i] = router->fPlayRing[router->fPlayRead];
			router->fPlayRead = (router->fPlayRead + 1)
				% router->fPlaySize;
		} else
			out[i] = 0;
	}
	(void)format;
}


void
SipAudioRouter::_RecordBuffer(void* cookie, bigtime_t timestamp, void* data,
	size_t size, const media_raw_audio_format& format)
{
	SipAudioRouter* router = (SipAudioRouter*)cookie;
	size_t frames = size / sizeof(int16);

	BAutolock lock(router->fLock);
	const int16* in = (const int16*)data;
	for (size_t i = 0; i < frames; i++) {
		size_t next = (router->fRecWrite + 1) % router->fRecSize;
		if (next == router->fRecRead)
			break;	// ring full; drop
		router->fRecRing[router->fRecWrite] = in[i];
		router->fRecWrite = next;
	}
	(void)timestamp;
	(void)format;
}


status_t
SipAudioRouter::BeginCall()
{
	media_raw_audio_format rawFormat =
		media_raw_audio_format::wildcard;
	rawFormat.format = media_raw_audio_format::B_AUDIO_SHORT;
	rawFormat.frame_rate = kAudioRate;
	rawFormat.channel_count = 1;
	rawFormat.byte_order = 0;
	rawFormat.buffer_size = 320;
	BMediaFormat format(rawFormat);

	fPlayer = new BSoundPlayer(&format, "SIP call speaker",
		&_PlayBuffer, NULL, this);
	if (fPlayer->InitCheck() != B_OK) {
		delete fPlayer;
		fPlayer = NULL;
		return B_ERROR;
	}

	fRecorder = new BMediaRecorder("SIP call microphone");
	fRecorder->SetFormat(format);
	fRecorder->SetHooks(&_RecordBuffer, NULL, this);
	if (fRecorder->InitCheck() != B_OK) {
		delete fRecorder;
		fRecorder = NULL;
		delete fPlayer;
		fPlayer = NULL;
		return B_ERROR;
	}

	fRecorder->Start();
	fPlayer->Start();

	// Hand the hooks to the backend; the rings shuttle samples
	// between the PipeWire streams and the G.711 encoder.
	BTelephonyRoster::Default()->SetAudioHooks(
		[](void* cookie, int16* samples, size_t max) -> size_t {
			SipAudioRouter* router = (SipAudioRouter*)cookie;
			BAutolock lock(router->fLock);
			size_t got = 0;
			while (got < max && router->fRecRead != router->fRecWrite) {
				samples[got++] = router->fRecRing[router->fRecRead];
				router->fRecRead = (router->fRecRead + 1)
					% router->fRecSize;
			}
			return got;
		},
		[](void* cookie, const int16* samples, size_t count) {
			SipAudioRouter* router = (SipAudioRouter*)cookie;
			BAutolock lock(router->fLock);
			for (size_t i = 0; i < count; i++) {
				size_t next = (router->fPlayWrite + 1)
					% router->fPlaySize;
				if (next == router->fPlayRead)
					break;
				router->fPlayRing[router->fPlayWrite] = samples[i];
				router->fPlayWrite = next;
			}
		},
		this);
	return B_OK;
}


status_t
SipAudioRouter::EndCall()
{
	BTelephonyRoster::Default()->SetAudioHooks(NULL, NULL, NULL);

	if (fPlayer != NULL) {
		fPlayer->Stop();
		delete fPlayer;
		fPlayer = NULL;
	}
	if (fRecorder != NULL) {
		fRecorder->Stop();
		delete fRecorder;
		fRecorder = NULL;
	}

	BAutolock lock(fLock);
	fPlayRead = fPlayWrite = 0;
	fRecRead = fRecWrite = 0;
	return B_OK;
}
