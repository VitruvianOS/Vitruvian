/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaClient.h>

#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

#include <ObjectList.h>

// PipeWire access goes through PipeWireBackend. The two artefacts that
// genuinely have to live here are the stream-event vtable and the
// realtime dequeue/queue calls — both reference Impl state.
#include <pipewire/stream.h>

#include "PipeWireBackend.h"


using namespace BPrivate::media;


typedef BObjectList<BMediaInput, false>  InputList;
typedef BObjectList<BMediaOutput, false> OutputList;


static std::atomic<media_client_id> sNextId{1};


struct BMediaClient::Impl {
	BMediaClient*		owner;

	media_client_id		id;
	BString				name;
	media_client_kinds	kinds;
	media_type			type;
	status_t			initErr;
	bool				running;
	bigtime_t			currentTime;

	InputList*			inputs;
	OutputList*			outputs;
	BControllable*		controllable;

	pw_stream*			stream;
	pw_direction		direction;
	uint32				frameStride;	// bytes per frame across channels
	BMediaFormat		streamFormat;

	static const pw_stream_events kStreamEvents;
	static void _OnProcess(void* userdata);
	static void _OnStateChanged(void* userdata, enum pw_stream_state old,
		enum pw_stream_state state, const char* error);
};


const pw_stream_events BMediaClient::Impl::kStreamEvents = {
	PW_VERSION_STREAM_EVENTS,
	/* destroy       */ NULL,
	/* state_changed */ &BMediaClient::Impl::_OnStateChanged,
	/* control_info  */ NULL,
	/* io_changed    */ NULL,
	/* param_changed */ NULL,
	/* add_buffer    */ NULL,
	/* remove_buffer */ NULL,
	/* process       */ &BMediaClient::Impl::_OnProcess,
	/* drained       */ NULL,
	/* command       */ NULL,
	/* trigger_done  */ NULL
};


BMediaClient::BMediaClient(const char* name, media_type type,
	media_client_kinds kinds)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->owner        = this;
	fImpl->id           = sNextId.fetch_add(1);
	fImpl->name         = name != NULL ? name : "";
	fImpl->kinds        = kinds;
	fImpl->type         = type;
	fImpl->initErr      = B_OK;
	fImpl->running      = false;
	fImpl->currentTime  = 0;
	fImpl->inputs       = new(std::nothrow) InputList();
	fImpl->outputs      = new(std::nothrow) OutputList();
	fImpl->controllable = NULL;
	fImpl->stream       = NULL;
	fImpl->direction    = PW_DIRECTION_OUTPUT;
	fImpl->frameStride  = 0;
}


BMediaClient::~BMediaClient()
{
	if (fImpl == NULL)
		return;
	Stop();
	delete fImpl->inputs;
	delete fImpl->outputs;
	delete fImpl;
}


media_client_id    BMediaClient::Id() const          { return fImpl->id; }
const char*        BMediaClient::Name() const        { return fImpl->name.String(); }
media_client_kinds BMediaClient::Kinds() const       { return fImpl->kinds; }
media_type         BMediaClient::MediaType() const   { return fImpl->type; }
status_t           BMediaClient::InitCheck() const   { return fImpl->initErr; }
bool               BMediaClient::IsStarted() const   { return fImpl->running; }
bigtime_t          BMediaClient::CurrentTime() const { return fImpl->currentTime; }
BControllable*     BMediaClient::Controllable() const { return fImpl->controllable; }


void BMediaClient::SetControllable(BControllable* c) { fImpl->controllable = c; }


status_t
BMediaClient::RegisterInput(BMediaInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;
	fImpl->inputs->AddItem(input);
	return B_OK;
}


status_t
BMediaClient::RegisterOutput(BMediaOutput* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	fImpl->outputs->AddItem(output);
	return B_OK;
}


status_t
BMediaClient::UnregisterInput(BMediaInput* input)
{
	fImpl->inputs->RemoveItem(input);
	return B_OK;
}


status_t
BMediaClient::UnregisterOutput(BMediaOutput* output)
{
	fImpl->outputs->RemoveItem(output);
	return B_OK;
}


int32  BMediaClient::CountInputs()  const { return fImpl->inputs->CountItems(); }
int32  BMediaClient::CountOutputs() const { return fImpl->outputs->CountItems(); }
BMediaInput*  BMediaClient::InputAt(int32 i) const  { return fImpl->inputs->ItemAt(i); }
BMediaOutput* BMediaClient::OutputAt(int32 i) const { return fImpl->outputs->ItemAt(i); }


status_t BMediaClient::Bind(BMediaInput*, BMediaOutput*)   { return B_NOT_SUPPORTED; }
status_t BMediaClient::Unbind(BMediaInput*, BMediaOutput*) { return B_NOT_SUPPORTED; }


status_t
BMediaClient::Start()
{
	if (fImpl->running)
		return B_OK;

	const int32 nOut = fImpl->outputs->CountItems();
	const int32 nIn  = fImpl->inputs->CountItems();
	if (nOut > 0 && nIn > 0)
		return B_NOT_SUPPORTED;	// filter mode needed; Cortex (Batch I)
	if (nOut == 0 && nIn == 0)
		return B_BAD_VALUE;
	fImpl->direction = (nOut > 0) ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	BMediaFormat fmt;
	status_t err = GetStreamFormat(&fmt);
	if (err != B_OK)
		return err;
	if (!fmt.IsRawAudio())
		return B_NOT_SUPPORTED;
	const media_raw_audio_format& raw = fmt.format.u.raw_audio;
	fImpl->streamFormat = fmt;
	fImpl->frameStride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* raw.channel_count;

	fImpl->stream = backend->CreateAndConnectStream(fImpl->name.String(),
		fImpl->direction, fmt, &Impl::kStreamEvents, fImpl);
	if (fImpl->stream == NULL)
		return B_ERROR;

	fImpl->running = true;
	HandleStart(0);
	return B_OK;
}


status_t
BMediaClient::Stop()
{
	if (fImpl->running)
		HandleStop(0);
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend != NULL)
		backend->DestroyStream(fImpl->stream);
	fImpl->stream = NULL;
	fImpl->running = false;
	return B_OK;
}


status_t
BMediaClient::Seek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	HandleSeek(mediaTime, performanceTime);
	return B_OK;
}


void BMediaClient::HandleStart(bigtime_t) {}
void BMediaClient::HandleStop(bigtime_t)  {}
void BMediaClient::HandleSeek(bigtime_t, bigtime_t) {}


void
BMediaClient::ProcessCallback(BMediaConnection* /*conn*/, void* buffer,
	size_t bufferSize, uint32 /*frames*/)
{
	// Default: produce silence on output, ignore on input.
	if (fImpl->direction == PW_DIRECTION_OUTPUT)
		memset(buffer, 0, bufferSize);
}


status_t
BMediaClient::GetStreamFormat(BMediaFormat* out) const
{
	if (out == NULL)
		return B_BAD_VALUE;
	// Default: use the first connection's format if any was set, otherwise
	// the F32LE stereo 48k default.
	BMediaConnection* conn = NULL;
	if (fImpl->outputs->CountItems() > 0)
		conn = fImpl->outputs->ItemAt(0);
	else if (fImpl->inputs->CountItems() > 0)
		conn = fImpl->inputs->ItemAt(0);

	if (conn != NULL && conn->Format().IsRawAudio()) {
		*out = conn->Format();
		return B_OK;
	}
	out->SetToDefault();
	return B_OK;
}


// #pragma mark - PipeWire callbacks


void
BMediaClient::Impl::_OnProcess(void* userdata)
{
	Impl* self = (Impl*)userdata;
	if (self == NULL || self->stream == NULL)
		return;

	pw_buffer* pb = pw_stream_dequeue_buffer(self->stream);
	if (pb == NULL)
		return;

	spa_buffer* spaBuf = pb->buffer;
	uint8_t* data = (uint8_t*)spaBuf->datas[0].data;
	if (data == NULL) {
		pw_stream_queue_buffer(self->stream, pb);
		return;
	}

	const uint32 stride = self->frameStride;
	uint32 maxFrames = stride > 0
		? spaBuf->datas[0].maxsize / stride : 0;
	if (self->direction == PW_DIRECTION_OUTPUT
			&& pb->requested > 0 && pb->requested < maxFrames) {
		maxFrames = pb->requested;
	}

	if (self->direction == PW_DIRECTION_INPUT) {
		// The driver fills datas[0] and reports the actual byte count.
		const uint32 actual = stride > 0
			? spaBuf->datas[0].chunk->size / stride : 0;
		if (actual < maxFrames)
			maxFrames = actual;
	}

	const size_t bytes = maxFrames * stride;

	BMediaConnection* conn = NULL;
	if (self->direction == PW_DIRECTION_OUTPUT
			&& self->outputs->CountItems() > 0) {
		conn = self->outputs->ItemAt(0);
	} else if (self->direction == PW_DIRECTION_INPUT
			&& self->inputs->CountItems() > 0) {
		conn = self->inputs->ItemAt(0);
	}

	self->owner->ProcessCallback(conn, data, bytes, maxFrames);

	if (self->direction == PW_DIRECTION_OUTPUT) {
		spaBuf->datas[0].chunk->offset = 0;
		spaBuf->datas[0].chunk->stride = stride;
		spaBuf->datas[0].chunk->size   = bytes;
	}
	pw_stream_queue_buffer(self->stream, pb);
}


void
BMediaClient::Impl::_OnStateChanged(void*, enum pw_stream_state, enum pw_stream_state,
	const char*)
{
}
