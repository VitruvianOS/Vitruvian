/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PeakMeter.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <Application.h>
#include <Message.h>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>

#include "PipeWireBackend.h"


namespace BPrivate { namespace media {


PeakMeter::PeakMeter(uint32 nodeId, bool capture, const char* targetNodeName)
	:
	fNodeId(nodeId),
	fCapture(capture),
	fStream(NULL),
	fPeak(0.0f),
	fInterval(0),
	fLastPost(0),
	fStatus(B_NO_INIT)
{
	if (nodeId == 0)
		return;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return;

	BMediaFormat meterFormat = BMediaFormatBuilder(B_MEDIA_RAW_AUDIO)
		.SetSampleFormat(media_raw_audio_format::B_AUDIO_FLOAT)
		.SetByteOrder(B_MEDIA_HOST_ENDIAN)
		.SetFrameRate(48000.0f)
		.SetChannelCount(1)
		.SetDefaultChannelPositions()
		.End();

	static const pw_stream_events kEvents = {
		PW_VERSION_STREAM_EVENTS,
		NULL,
		&PeakMeter::_OnStateChanged,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		&PeakMeter::_OnProcess,
		NULL,
	};

	pw_stream* stream = backend->CreateAndConnectStream(
		PipeWireBackend::kPeakMeterNodeName,
		PW_DIRECTION_INPUT,
		meterFormat,
		&kEvents, this,
		targetNodeName != NULL && targetNodeName[0] != '\0'
			? targetNodeName : NULL,
		!capture);
	if (stream == NULL)
		return;

	fStream  = stream;
	fStatus  = B_OK;
}


PeakMeter::~PeakMeter()
{
	if (fStream != NULL) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL)
			backend->DestroyStream((pw_stream*)fStream);
	}
}


status_t
PeakMeter::InitCheck() const
{
	return fStatus;
}


float
PeakMeter::Peak() const
{
	return fPeak.load(std::memory_order_relaxed);
}


void
PeakMeter::Reset()
{
	fPeak.store(0.0f, std::memory_order_relaxed);
}


void
PeakMeter::StartPosting(const BMessenger& target, bigtime_t intervalUs)
{
	std::lock_guard<std::mutex> _(fTargetLock);
	fTarget = target;
	fInterval = intervalUs;
	fLastPost = system_time();
}


void
PeakMeter::StopPosting()
{
	std::lock_guard<std::mutex> _(fTargetLock);
	fTarget = BMessenger();
	fInterval = 0;
}


void
PeakMeter::_OnProcess(void* userData)
{
	PeakMeter* self = (PeakMeter*)userData;
	pw_stream* stream = (pw_stream*)self->fStream;
	if (stream == NULL)
		return;

	pw_buffer* b = pw_stream_dequeue_buffer(stream);
	if (b == NULL || b->buffer == NULL || b->buffer->datas == NULL
		|| b->buffer->datas[0].data == NULL) {
		if (b != NULL)
			pw_stream_queue_buffer(stream, b);
		return;
	}

	const spa_chunk* chunk = b->buffer->datas[0].chunk;
	if (chunk != NULL && chunk->size > 0) {
		const float* samples = (const float*)b->buffer->datas[0].data;
		uint32 count = chunk->size / sizeof(float);
		float max = 0.0f;
		for (uint32 i = 0; i < count; i++) {
			float v = fabsf(samples[i]);
			if (v > max)
				max = v;
		}

		float prev = self->fPeak.load(std::memory_order_relaxed);
		if (max < prev)
			max = prev * 0.85f;
		self->fPeak.store(max, std::memory_order_relaxed);
	}
	pw_stream_queue_buffer(stream, b);

	BMessenger target;
	bigtime_t interval;
	bigtime_t lastPost;
	{
		std::lock_guard<std::mutex> _(self->fTargetLock);
		target = self->fTarget;
		interval = self->fInterval;
		lastPost = self->fLastPost;
	}
	if (target.IsValid() && interval > 0) {
		bigtime_t now = system_time();
		if (now - lastPost >= interval) {
			BMessage msg(kMsgPeakUpdate);
			msg.AddFloat("peak", self->Peak());
			msg.AddUInt32("node_id", self->fNodeId);
			target.SendMessage(&msg);
			self->fLastPost = now;
		}
	}
}


void
PeakMeter::_OnStateChanged(void*,
	enum pw_stream_state, enum pw_stream_state newState,
	const char*)
{
}


} }
