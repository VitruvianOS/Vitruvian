/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaClient.h>
#include <media2/MediaGraph.h>

#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

#include <ObjectList.h>

#include <pipewire/loop.h>
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
	status_t			initErr;
	bool				running;
	bigtime_t			currentTime;

	InputList*			inputs;
	OutputList*			outputs;
	BControllable*		controllable;

	bool				needsFilter;

	// Deferred fault handling: state_changed runs inside the stream's own emit, where destroying the
	// stream would be a use-after-free, so it only flags fStreamFault and wakes this event source.
	pw_loop*			faultLoop;
	spa_source*			faultEvent;

	static const pw_stream_events kStreamEvents;
	static void _OnProcess(void* userdata);
	static void _OnStateChanged(void* userdata, enum pw_stream_state old,
		enum pw_stream_state state, const char* error);
	static void _OnFaultEvent(void* userdata, uint64_t count);
};


const pw_stream_events BMediaClient::Impl::kStreamEvents = {
	.version = PW_VERSION_STREAM_EVENTS,
	.state_changed = &BMediaClient::Impl::_OnStateChanged,
	.process = &BMediaClient::Impl::_OnProcess,
};


BMediaClient::BMediaClient(const char* name, media_client_kinds kinds)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->owner        = this;
	fImpl->id           = sNextId.fetch_add(1);
	fImpl->name         = name != NULL ? name : "";
	fImpl->kinds        = kinds;
	fImpl->initErr      = B_OK;
	fImpl->running      = false;
	fImpl->currentTime  = 0;
	fImpl->inputs       = new(std::nothrow) InputList();
	fImpl->outputs      = new(std::nothrow) OutputList();
	fImpl->controllable = NULL;
	fImpl->needsFilter  = (kinds & B_MEDIA_FILTER) == B_MEDIA_FILTER;
	fImpl->faultLoop    = NULL;
	fImpl->faultEvent   = NULL;
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
status_t           BMediaClient::InitCheck() const   { return fImpl->initErr; }


media_type_mask
BMediaClient::MediaTypes() const
{
	media_type_mask mask = B_MEDIA_NO_TYPES;
	for (int32 i = 0; i < fImpl->inputs->CountItems(); i++)
		mask |= fImpl->inputs->ItemAt(i)->AcceptedTypes();
	for (int32 i = 0; i < fImpl->outputs->CountItems(); i++)
		mask |= fImpl->outputs->ItemAt(i)->AcceptedTypes();
	return mask;
}


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


status_t
BMediaClient::Bind(BMediaInput* input, BMediaOutput* output)
{
	if (input == NULL || output == NULL)
		return B_BAD_VALUE;

	if (input->Client() == NULL || output->Client() == NULL)
		return B_BAD_VALUE;

	if (input->Client() != this || output->Client() != this)
		return B_BAD_VALUE;

	media_type_mask inputTypes = input->AcceptedTypes();
	media_type_mask outputTypes = output->AcceptedTypes();
	if ((inputTypes & outputTypes) == 0)
		return B_BAD_VALUE;

	input->fBinding = output;
	output->fBinding = input;

	return B_OK;
}


status_t
BMediaClient::Unbind(BMediaInput* input, BMediaOutput* output)
{
	if (input == NULL || output == NULL)
		return B_BAD_VALUE;

	if (input->fBinding != output || output->fBinding != input)
		return B_ENTRY_NOT_FOUND;

	input->fBinding = NULL;
	output->fBinding = NULL;

	return B_OK;
}


status_t
BMediaClient::Connect(BMediaOutput* output, BMediaInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	bool outputBelongsToThis = (output->Client() == this);
	bool inputBelongsToThis = (input->Client() == this);
	if (!outputBelongsToThis && !inputBelongsToThis)
		return B_BAD_VALUE;

	if (outputBelongsToThis && inputBelongsToThis)
		return B_BAD_VALUE;

	return graph->Connect(output, input);
}


status_t
BMediaClient::Disconnect(BMediaOutput* output, BMediaInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	bool outputBelongsToThis = (output->Client() == this);
	bool inputBelongsToThis = (input->Client() == this);
	if (!outputBelongsToThis && !inputBelongsToThis)
		return B_BAD_VALUE;

	if (outputBelongsToThis && inputBelongsToThis)
		return B_BAD_VALUE;

	return graph->Disconnect(output, input);
}


status_t
BMediaClient::Start()
{
	if (fImpl->running)
		return B_OK;

	const int32 nOut = fImpl->outputs->CountItems();
	const int32 nIn  = fImpl->inputs->CountItems();
	if (nOut == 0 && nIn == 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	status_t result = _StartConnections(backend);
	if (result != B_OK)
		return result;

	if (fImpl->faultEvent == NULL) {
		backend->Lock();
		fImpl->faultLoop = backend->GetMainLoop();
		fImpl->faultEvent = pw_loop_add_event(fImpl->faultLoop,
			&Impl::_OnFaultEvent, fImpl);
		backend->Unlock();
	}

	fImpl->running = true;
	HandleStart(0);
	return B_OK;
}


status_t
BMediaClient::Stop()
{
	if (fImpl->running)
		HandleStop(0);
	// Clear before tearing streams down: destroying a pw_stream can reenter state_changed, which
	// uses IsStarted() to tell an intentional stop from a graph-side drop.
	fImpl->running = false;

	if (fImpl->faultEvent != NULL) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL) {
			// Locking waits out any fault-event dispatch in flight before the source is destroyed.
			backend->Lock();
			pw_loop_destroy_source(fImpl->faultLoop, fImpl->faultEvent);
			backend->Unlock();
		}
		fImpl->faultEvent = NULL;
		fImpl->faultLoop = NULL;
	}

	_StopConnections();
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


status_t
BMediaClient::_StartConnections(void*)
{
	return B_NOT_SUPPORTED;
}


void
BMediaClient::_StopConnections()
{
}


void
BMediaClient::_StreamFault(BMediaConnection*)
{
}


const pw_stream_events*
BMediaClient::_GetStreamEvents()
{
	return &Impl::kStreamEvents;
}


void
BMediaClient::ProcessCallback(BMediaConnection* conn, void* buffer,
	size_t bufferSize, uint32)
{
	if (conn == NULL)
		return;

	BMediaInput* input = dynamic_cast<BMediaInput*>(conn);
	if (input != NULL) {
		input->HandleBuffer(buffer, bufferSize, conn->Format());
		return;
	}

	BMediaOutput* output = dynamic_cast<BMediaOutput*>(conn);
	if (output != NULL) {
		size_t filled = output->FillBuffer(buffer, bufferSize);
		if (filled < bufferSize)
			memset((uint8*)buffer + filled, 0, bufferSize - filled);
		return;
	}

	memset(buffer, 0, bufferSize);
}


void
BMediaClient::Impl::_OnProcess(void* userdata)
{
	BMediaConnection* conn = (BMediaConnection*)userdata;
	if (conn == NULL || conn->Client() == NULL)
		return;

	pw_stream* stream = (pw_stream*)conn->fStream;
	if (stream == NULL)
		return;

	pw_buffer* pb = pw_stream_dequeue_buffer(stream);
	if (pb == NULL)
		return;

	spa_buffer* spaBuf = pb->buffer;
	uint8* data = (uint8*)spaBuf->datas[0].data;
	if (data == NULL) {
		pw_stream_queue_buffer(stream, pb);
		return;
	}

	const BMediaFormat& format = conn->Format();
	if (!format.IsRawAudio()) {
		pw_stream_queue_buffer(stream, pb);
		return;
	}

	const media_raw_audio_format& raw = format.format.u.raw_audio;
	const uint32 stride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* raw.channel_count;
	uint32 maxFrames = stride > 0 ? spaBuf->datas[0].maxsize / stride : 0;
	if (pb->requested > 0 && pb->requested < maxFrames)
		maxFrames = pb->requested;

	BMediaOutput* output = dynamic_cast<BMediaOutput*>(conn);
	if (output != NULL) {
		const size_t bytes = maxFrames * stride;
		size_t filled = output->FillBuffer(data, bytes);
		if (filled < bytes)
			memset(data + filled, 0, bytes - filled);
		spaBuf->datas[0].chunk->offset = 0;
		spaBuf->datas[0].chunk->stride = stride;
		spaBuf->datas[0].chunk->size   = bytes;
	} else {
		BMediaInput* input = dynamic_cast<BMediaInput*>(conn);
		if (input != NULL) {
			const uint32 actual = stride > 0
				? spaBuf->datas[0].chunk->size / stride : 0;
			if (actual < maxFrames)
				maxFrames = actual;
			const size_t bytes = maxFrames * stride;
			input->HandleBuffer(data, bytes, format);
		}
	}

	pw_stream_queue_buffer(stream, pb);
}


void
BMediaClient::Impl::_OnStateChanged(void* userdata, enum pw_stream_state,
	enum pw_stream_state state, const char*)
{
	BMediaConnection* conn = (BMediaConnection*)userdata;
	if (conn == NULL)
		return;

	if (state != PW_STREAM_STATE_UNCONNECTED && state != PW_STREAM_STATE_ERROR)
		return;

	conn->Disconnected();

	// A drop while running means the far end went away, e.g. an ALSA reset across a suspend; Stop() clears
	// running first. Rebuilding here would be a use-after-free, so flag it for _OnFaultEvent().
	BMediaClient* client = conn->Client();
	if (client == NULL || !client->IsStarted())
		return;

	conn->fStreamFault.store(true);
	if (client->fImpl->faultEvent != NULL)
		pw_loop_signal_event(client->fImpl->faultLoop, client->fImpl->faultEvent);
}


void
BMediaClient::Impl::_OnFaultEvent(void* userdata, uint64_t)
{
	Impl* impl = (Impl*)userdata;
	if (impl == NULL || impl->owner == NULL || !impl->running)
		return;

	// Re-check everything: this runs asynchronously, so the client may have stopped, restarted or reconnected.
	for (int32 i = 0; i < impl->outputs->CountItems(); i++) {
		BMediaOutput* output = impl->outputs->ItemAt(i);
		if (output != NULL && output->fStreamFault.exchange(false)
				&& impl->running) {
			impl->owner->_StreamFault(output);
		}
	}
	for (int32 i = 0; i < impl->inputs->CountItems(); i++) {
		BMediaInput* input = impl->inputs->ItemAt(i);
		if (input != NULL && input->fStreamFault.exchange(false)
				&& impl->running) {
			impl->owner->_StreamFault(input);
		}
	}
}
