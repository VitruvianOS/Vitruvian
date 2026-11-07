/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _SIP_AUDIO_ROUTER_H
#define _SIP_AUDIO_ROUTER_H


#include <Locker.h>
#include <media2/MediaDefs.h>


class BMediaRecorder;
class BSoundPlayer;


// Pipes SIP call audio between the backend's G.711 hooks and the
// media kit, whose SoundPlayer and MediaRecorder ride the PipeWire
// backend. Started when a SIP call goes active, stopped when it
// ends; modem lines keep using the UCM voice verb instead.
class SipAudioRouter {
public:
	static SipAudioRouter*	Default();

								SipAudioRouter();
								~SipAudioRouter();

			status_t			BeginCall();
			status_t			EndCall();

private:
			static	void		_PlayBuffer(void* cookie, void* buffer,
									size_t size,
									const media_raw_audio_format& format);
			static	void		_RecordBuffer(void* cookie,
									bigtime_t timestamp, void* data,
									size_t size,
									const media_raw_audio_format& format);

			BSoundPlayer*		fPlayer;
			BMediaRecorder*		fRecorder;
			BLocker				fLock;
			// Far-end PCM waiting for the speaker.
			int16*				fPlayRing;
			size_t				fPlayRead;
			size_t				fPlayWrite;
			size_t				fPlaySize;
			// Mic PCM waiting for the encoder.
			int16*				fRecRing;
			size_t				fRecRead;
			size_t				fRecWrite;
			size_t				fRecSize;
};


#endif // _SIP_AUDIO_ROUTER_H
