/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Real-time peak/level meter for a PipeWire node. Subscribes to a
// PipeWire node's output via a passive pw_stream and computes an
// RMS/peak reading on the PipeWire thread; UI threads poll Peak()
// or receive BMessage updates.

#ifndef _VITRUVIAN_MEDIA2_PEAK_METER_H
#define _VITRUVIAN_MEDIA2_PEAK_METER_H


#include <SupportDefs.h>
#include <Messenger.h>

#include <atomic>
#include <mutex>

#include <pipewire/stream.h>


namespace BPrivate { namespace media {


class PeakMeter {
public:
			PeakMeter(uint32 nodeId, bool capture = false,
				const char* targetNodeName = NULL);
	virtual	~PeakMeter();

			status_t	InitCheck() const;

			float		Peak() const;
			void		Reset();

	static	const uint32 kMsgPeakUpdate = 'PWPk';
			void		StartPosting(const BMessenger& target, bigtime_t intervalUs);
			void		StopPosting();

private:
	static	void		_OnProcess(void* userData);
	static	void		_OnStateChanged(void* userData,
						enum pw_stream_state oldState,
						enum pw_stream_state newState,
						const char* error);

			uint32		fNodeId;
			bool		fCapture;
			void*		fStream;
			std::atomic<float> fPeak;
			std::mutex	fTargetLock;
			BMessenger	fTarget;
			bigtime_t	fInterval;
			bigtime_t	fLastPost;
			status_t	fStatus;
};


} }


#endif
