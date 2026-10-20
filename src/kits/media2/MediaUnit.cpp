/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaUnit.h>

#include <new>
#include <string.h>
#include <vector>

#include <atomic>

#include <Autolock.h>
#include <Locker.h>
#include <OS.h>

#include <pipewire/filter.h>
#include <pipewire/loop.h>
#include <spa/param/audio/format.h>
#include <spa/param/props.h>
#include <spa/utils/type.h>
#include <spa/pod/builder.h>

#include "PipeWireBackend.h"


// The public property constants are the SPA values.
static_assert((int)B_MEDIA_PROP_CUSTOM_BASE == (int)SPA_PROP_START_CUSTOM,
	"custom property base");
static_assert((int)B_MEDIA_PROP_BOOL == (int)SPA_TYPE_Bool
	&& (int)B_MEDIA_PROP_ID == (int)SPA_TYPE_Id
	&& (int)B_MEDIA_PROP_INT32 == (int)SPA_TYPE_Int
	&& (int)B_MEDIA_PROP_INT64 == (int)SPA_TYPE_Long
	&& (int)B_MEDIA_PROP_FLOAT == (int)SPA_TYPE_Float
	&& (int)B_MEDIA_PROP_DOUBLE == (int)SPA_TYPE_Double
	&& (int)B_MEDIA_PROP_STRING == (int)SPA_TYPE_String,
	"property value types");


// PipeWire 1.2.0 and later declare this; it is only a property key, and an
// older server ignores one it doesn't know.
#ifndef PW_KEY_PORT_GROUP
#	define PW_KEY_PORT_GROUP "port.group"
#endif


using namespace BPrivate::media;


struct BMediaUnit::Impl {
	BMediaUnit*				owner;

	pw_filter*				filter;
	spa_hook				listener;
	uint32					quantum;
	uint32					sampleRate;
	bigtime_t				cycleStartTime;

	// Same deferred-fault scheme as BMediaClient (see MediaClient.cpp): state_changed only flags
	// faultPending and wakes faultEvent, and _OnFaultEvent() rebuilds outside any emit.
	std::atomic<bool>		faultPending;
	pw_loop*				faultLoop;
	spa_source*				faultEvent;

	// Detached mode. detachLock serializes senders and guards the buffers.
	struct DetachedBuffer {
		BMediaConnection*		connection;
		std::vector<uint8>		storage;
		void*					data;
		size_t					size;
	};

	BLocker					detachLock;
	bool					detachedOnly;
	bool					detachedMode;
	int32					detachedLinks;
	BMediaInput*			drivingInput;
	std::vector<DetachedBuffer> detachedBuffers;

	Impl()
		:
		detachLock("detached unit"),
		detachedOnly(false),
		detachedMode(false),
		detachedLinks(0),
		drivingInput(NULL)
	{
	}

	DetachedBuffer*			_Detached(BMediaConnection* connection)
	{
		for (size_t i = 0; i < detachedBuffers.size(); i++) {
			if (detachedBuffers[i].connection == connection)
				return &detachedBuffers[i];
		}
		return NULL;
	}

	static const pw_filter_events kFilterEvents;
	static void _OnProcess(void* data, struct spa_io_position* position);
	static void _OnStateChanged(void* data, enum pw_filter_state old,
		enum pw_filter_state state, const char* error);
	static void _OnFaultEvent(void* data, uint64_t count);
};


const pw_filter_events BMediaUnit::Impl::kFilterEvents = {
	.version = PW_VERSION_FILTER_EVENTS,
	.state_changed = &BMediaUnit::Impl::_OnStateChanged,
	.process = &BMediaUnit::Impl::_OnProcess,
};


struct port_data {
	BMediaConnection*		connection;
	uint32					direction;
};


BMediaUnit::BMediaUnit(const char* name, media_client_kinds kinds)
	:
	BMediaClient(name, kinds),
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl == NULL)
		return;
	fImpl->owner      = this;
	fImpl->filter     = NULL;
	fImpl->quantum    = 0;
	fImpl->sampleRate = 0;
	fImpl->cycleStartTime = 0;
	fImpl->faultPending = false;
	fImpl->faultLoop  = NULL;
	fImpl->faultEvent = NULL;
	memset(&fImpl->listener, 0, sizeof(fImpl->listener));
}


BMediaUnit::~BMediaUnit()
{
	if (fImpl == NULL)
		return;
	Stop();
	delete fImpl;
}


bigtime_t
BMediaUnit::CycleStartTime() const
{
	return fImpl != NULL ? fImpl->cycleStartTime : 0;
}


uint32
BMediaUnit::Quantum() const
{
	return fImpl != NULL ? fImpl->quantum : 0;
}


uint32
BMediaUnit::SampleRate() const
{
	return fImpl != NULL ? fImpl->sampleRate : 0;
}


status_t
BMediaUnit::RegisterInput(BMediaInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;
	return BMediaClient::RegisterInput(input);
}


status_t
BMediaUnit::RegisterOutput(BMediaOutput* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;
	return BMediaClient::RegisterOutput(output);
}


status_t
BMediaUnit::UnregisterInput(BMediaInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;
	return BMediaClient::UnregisterInput(input);
}


status_t
BMediaUnit::UnregisterOutput(BMediaOutput* output)
{
	if (output == NULL)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;
	return BMediaClient::UnregisterOutput(output);
}


status_t
BMediaUnit::Bind(BMediaInput* input, BMediaOutput* output)
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
BMediaUnit::Unbind(BMediaInput* input, BMediaOutput* output)
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
BMediaUnit::Start()
{
	if (fImpl == NULL)
		return B_NO_INIT;
	if (fImpl->filter != NULL)
		return B_OK;

	// Detached: no pw_filter, the in-process sender drives the cycle.
	if (IsDetached())
		return IsStarted() ? B_OK : BMediaClient::Start();

	status_t result = _BuildFilter();
	if (result != B_OK)
		return result;

	if (fImpl->faultEvent == NULL) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL) {
			backend->Lock();
			fImpl->faultLoop = backend->GetMainLoop();
			fImpl->faultEvent = pw_loop_add_event(fImpl->faultLoop,
				&Impl::_OnFaultEvent, fImpl);
			backend->Unlock();
		}
	}

	return BMediaClient::Start();
}


status_t
BMediaUnit::_StartConnections(void*)
{
	// The filter built in Start() is the connection; no streams to open.
	return B_OK;
}


status_t
BMediaUnit::_BuildFilter()
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	const int32 nOut = CountOutputs();
	const int32 nIn  = CountInputs();
	if (nOut == 0 && nIn == 0)
		return B_BAD_VALUE;

	int r;

	backend->Lock();
	pw_properties* props = pw_properties_new(
		PW_KEY_APP_NAME, Name(),
		PW_KEY_NODE_NAME, Name(),
		PW_KEY_NODE_DESCRIPTION, Name(),
		PW_KEY_MEDIA_CLASS, "Audio/Filter",
		PW_KEY_NODE_GROUP, Name(),
		NULL);
	pw_filter* filter = backend->CreateFilter(Name(), props);
	backend->Unlock();

	if (filter == NULL)
		return B_ERROR;

	backend->Lock();
	pw_filter_add_listener(filter, &fImpl->listener, &fImpl->kFilterEvents,
		fImpl);
	backend->Unlock();

	for (int32 i = 0; i < nOut; i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL)
			continue;

		port_data* pd = new(std::nothrow) port_data();
		if (pd == NULL)
			goto error;
		pd->connection = output;
		pd->direction  = PW_DIRECTION_OUTPUT;

		const char* formatDSP = NULL;
		if (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO))
			formatDSP = "32 bit float mono audio";
		else if (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI))
			formatDSP = "8 bit raw midi";

		if (formatDSP == NULL)
			formatDSP = "32 bit float mono audio";

		pw_properties* portProps = pw_properties_new(
			PW_KEY_FORMAT_DSP, formatDSP,
			PW_KEY_PORT_NAME, output->Name(),
			PW_KEY_PORT_GROUP, Name(),
			NULL);

		backend->Lock();
		void* port = pw_filter_add_port(filter,
			PW_DIRECTION_OUTPUT,
			PW_FILTER_PORT_FLAG_MAP_BUFFERS,
			sizeof(port_data),
			portProps,
			NULL,
			0);
		backend->Unlock();

		if (port == NULL) {
			delete pd;
			goto error;
		}

		memcpy(port, pd, sizeof(port_data));
		output->_SetFilterPort(port);
	}

	for (int32 i = 0; i < nIn; i++) {
		BMediaInput* input = InputAt(i);
		if (input == NULL)
			continue;

		port_data* pd = new(std::nothrow) port_data();
		if (pd == NULL)
			goto error;
		pd->connection = input;
		pd->direction  = PW_DIRECTION_INPUT;

		const char* formatDSP = NULL;
		if (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO))
			formatDSP = "32 bit float mono audio";
		else if (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI))
			formatDSP = "8 bit raw midi";

		if (formatDSP == NULL)
			formatDSP = "32 bit float mono audio";

		pw_properties* portProps = pw_properties_new(
			PW_KEY_FORMAT_DSP, formatDSP,
			PW_KEY_PORT_NAME, input->Name(),
			PW_KEY_PORT_GROUP, Name(),
			NULL);

		backend->Lock();
		void* port = pw_filter_add_port(filter,
			PW_DIRECTION_INPUT,
			PW_FILTER_PORT_FLAG_MAP_BUFFERS,
			sizeof(port_data),
			portProps,
			NULL,
			0);
		backend->Unlock();

		if (port == NULL) {
			delete pd;
			goto error;
		}

		memcpy(port, pd, sizeof(port_data));
		input->_SetFilterPort(port);
	}

	backend->Lock();
	r = pw_filter_connect(filter,
		PW_FILTER_FLAG_RT_PROCESS,
		NULL,
		0);
	backend->Unlock();

	if (r < 0)
		goto error;

	fImpl->filter = filter;
	return B_OK;

error:
	backend->Lock();
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL && output->_GetFilterPort() != NULL)
			output->_SetFilterPort(NULL);
	}
	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL && input->_GetFilterPort() != NULL)
			input->_SetFilterPort(NULL);
	}
	if (filter != NULL)
		pw_filter_destroy(filter);
	backend->Unlock();

	return B_ERROR;
}


status_t
BMediaUnit::Stop()
{
	if (fImpl == NULL)
		return B_OK;

	if (fImpl->filter == NULL) {
		if (!IsDetached() || !IsStarted())
			return B_OK;
		status_t result = BMediaClient::Stop();
		BAutolock _(fImpl->detachLock);
		if (fImpl->detachedLinks == 0 && !fImpl->detachedOnly)
			fImpl->detachedMode = false;
		return result;
	}

	if (fImpl->faultEvent != NULL) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL) {
			// As in BMediaClient::Stop(), this waits out any fault-event dispatch before the source is destroyed.
			backend->Lock();
			pw_loop_destroy_source(fImpl->faultLoop, fImpl->faultEvent);
			backend->Unlock();
		}
		fImpl->faultEvent = NULL;
		fImpl->faultLoop = NULL;
	}

	_TeardownFilter();

	return BMediaClient::Stop();
}


void
BMediaUnit::_TeardownFilter()
{
	if (fImpl == NULL || fImpl->filter == NULL)
		return;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return;

	backend->Lock();
	pw_filter* filter = fImpl->filter;
	// Clear before destroying: pw_filter_destroy() can reenter state_changed, which uses a NULL
	// fImpl->filter to spot an intentional teardown.
	fImpl->filter = NULL;
	pw_filter_destroy(filter);

	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL && output->_GetFilterPort() != NULL)
			output->_SetFilterPort(NULL);
	}
	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL && input->_GetFilterPort() != NULL)
			input->_SetFilterPort(NULL);
	}
	backend->Unlock();
}


void
BMediaUnit::_Restart()
{
	// The filter's node died under us (graph restart or ALSA reset across a suspend). Rebuild from
	// scratch, which also gives a fresh clock/latency estimate.
	if (fImpl == NULL || !IsStarted())
		return;

	_TeardownFilter();
	_BuildFilter();
}


void
BMediaUnit::ProcessCycle(uint32 frameCount)
{
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL)
			continue;

		// MIDI ports are serviced by _OnProcess() around this call. Pulling
		// one of their pw_buffers here would strand it outside both queues
		// and starve FillBuffer after a few cycles.
		if ((output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) != 0)
			continue;

		if (!output->HasBinding()) {
			size_t size;
			void* buffer = BufferFor(output, &size);
			if (buffer != NULL && size > 0)
				memset(buffer, 0, size);
		} else {
			BMediaConnection* binding = output->Binding();
			if (binding != NULL) {
				BMediaInput* input = dynamic_cast<BMediaInput*>(binding);
				if (input != NULL) {
					size_t inSize, outSize;
					void* inBuffer = BufferFor(input, &inSize);
					void* outBuffer = BufferFor(output, &outSize);

					if (inBuffer != NULL && outBuffer != NULL) {
						size_t copySize = inSize < outSize ? inSize : outSize;
						memcpy(outBuffer, inBuffer, copySize);
						if (outSize > copySize)
							memset((uint8*)outBuffer + copySize, 0,
								outSize - copySize);
					}
				}
			}
		}
	}
}


void*
BMediaUnit::BufferFor(BMediaConnection* connection, size_t* outSize)
{
	if (connection != NULL && connection->fDetachedUnit == this)
		return _DetachedBufferFor(connection, outSize);

	if (connection == NULL || connection->_GetFilterPort() == NULL)
		return NULL;

	port_data* pd = (port_data*)connection->_GetFilterPort();
	if (pd == NULL)
		return NULL;

	if (connection->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO)) {
		void* buffer = pw_filter_get_dsp_buffer(pd, fImpl->quantum);
		if (buffer != NULL && outSize != NULL)
			*outSize = fImpl->quantum * sizeof(float);
		return buffer;
	}

	// MIDI ports never come through here: _OnProcess() hands the cycle's
	// sequence to FillBuffer/HandleBuffer directly. Dequeuing a pw_buffer
	// for the caller would strand it outside both queues.
	if (outSize != NULL)
		*outSize = 0;
	return NULL;
}


void
BMediaUnit::Impl::_OnProcess(void* data, struct spa_io_position* position)
{
	BMediaUnit::Impl* impl = (BMediaUnit::Impl*)data;
	if (impl == NULL || impl->owner == NULL)
		return;

	if (position != NULL && position->clock.rate.denom != 0) {
		impl->quantum = position->clock.duration;
		impl->sampleRate = position->clock.rate.denom;
		impl->cycleStartTime = position->clock.nsec / 1000;
	}

	// Deliver MIDI inputs first so ProcessCycle can walk this cycle's events,
	// then fill MIDI outputs from their rings. Audio paths stay in ProcessCycle.
	for (int32 i = 0; i < impl->owner->CountInputs(); i++) {
		BMediaInput* input = impl->owner->InputAt(i);
		if (input == NULL || (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) == 0)
			continue;
		if (input->_GetFilterPort() == NULL)
			continue;

		// MIDI DSP ports carry a POD sequence in the DSP buffer.
		port_data* pd = (port_data*)input->_GetFilterPort();
		void* seq = pw_filter_get_dsp_buffer(pd, impl->quantum);
		if (seq != NULL)
			input->HandleBuffer(seq, impl->quantum * sizeof(float),
				input->Format());
	}

	impl->owner->ProcessCycle(impl->quantum);

	for (int32 i = 0; i < impl->owner->CountOutputs(); i++) {
		BMediaOutput* output = impl->owner->OutputAt(i);
		if (output == NULL || (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) == 0)
			continue;
		if (output->_GetFilterPort() == NULL)
			continue;

		port_data* pd = (port_data*)output->_GetFilterPort();
		void* seq = pw_filter_get_dsp_buffer(pd, impl->quantum);
		if (seq != NULL)
			output->FillBuffer(seq, impl->quantum * sizeof(float));
	}
}


void
BMediaUnit::Impl::_OnStateChanged(void* data, enum pw_filter_state,
	enum pw_filter_state state, const char*)
{
	BMediaUnit::Impl* impl = (BMediaUnit::Impl*)data;
	if (impl == NULL || impl->owner == NULL)
		return;

	if (state == PW_FILTER_STATE_UNCONNECTED || state == PW_FILTER_STATE_ERROR) {
		for (int32 i = 0; i < impl->owner->CountInputs(); i++) {
			BMediaInput* input = impl->owner->InputAt(i);
			if (input != NULL)
				input->Disconnected();
		}
		for (int32 i = 0; i < impl->owner->CountOutputs(); i++) {
			BMediaOutput* output = impl->owner->OutputAt(i);
			if (output != NULL)
				output->Disconnected();
		}

		// An unexpected drop while started means the graph lost our node. Rebuilding here would destroy
		// the filter mid-emit, so flag it and wake the fault event for _OnFaultEvent() to restart.
		if (impl->filter != NULL && impl->owner->IsStarted()) {
			impl->faultPending.store(true);
			if (impl->faultEvent != NULL)
				pw_loop_signal_event(impl->faultLoop, impl->faultEvent);
		}
	}
}


void
BMediaUnit::Impl::_OnFaultEvent(void* data, uint64_t)
{
	Impl* impl = (Impl*)data;
	if (impl == NULL || impl->owner == NULL)
		return;

	// Re-check here: this runs asynchronously, so the unit may have been stopped or restarted.
	if (impl->faultPending.exchange(false) && impl->filter != NULL
			&& impl->owner->IsStarted()) {
		impl->owner->_Restart();
	}
}


void*
BMediaUnit::_GetFilter() const
{
	return fImpl != NULL ? fImpl->filter : NULL;
}


uint32
BMediaUnit::NodeId() const
{
	if (fImpl == NULL || fImpl->filter == NULL)
		return 0;
	return pw_filter_get_node_id(fImpl->filter);
}


status_t
BMediaUnit::PublishNodeParams(const uint32* propIds,
	const char* const* names, const uint32* valueTypes,
	const float* minValues, const float* maxValues, const float* steps,
	const void* const* values, const size_t* valueSizes, int32 count)
{
	if (fImpl == NULL || fImpl->filter == NULL)
		return B_NO_INIT;
	if (propIds == NULL || names == NULL || valueTypes == NULL
			|| count <= 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_ERROR;

	std::vector<PipeWireBackend::NodePropInfo> infos;
	std::vector<PipeWireBackend::NodePropValue> vals;
	infos.reserve((size_t)count);
	vals.reserve((size_t)count);

	for (int32 i = 0; i < count; i++) {
		PipeWireBackend::NodePropInfo info;
		info.id = propIds[i];
		info.name = names[i] != NULL ? names[i] : "";
		info.valueType = valueTypes[i];
		info.minValue = minValues != NULL ? minValues[i] : 0.0f;
		info.maxValue = maxValues != NULL ? maxValues[i] : 0.0f;
		info.step = steps != NULL ? steps[i] : 0.0f;
		info.hasRange = maxValues != NULL && maxValues[i] > minValues[i];
		infos.push_back(info);

		if (values != NULL && valueSizes != NULL && values[i] != NULL
				&& valueSizes[i] > 0) {
			PipeWireBackend::NodePropValue v;
			v.id = propIds[i];
			v.valueType = valueTypes[i];
			v.bytes.assign((const uint8_t*)values[i],
				(const uint8_t*)values[i] + valueSizes[i]);
			vals.push_back(v);
		}
	}

	return backend->PublishFilterParams(fImpl->filter, infos, vals);
}


// #pragma mark - detached mode


bool
BMediaUnit::IsInProcessRouted() const
{
	return IsDetached();
}


bool
BMediaUnit::IsDetached() const
{
	return fImpl != NULL && (fImpl->detachedOnly || fImpl->detachedMode);
}


bool
BMediaUnit::IsDetachedOnly() const
{
	return fImpl != NULL && fImpl->detachedOnly;
}


void
BMediaUnit::SetDetachedOnly(bool detachedOnly)
{
	if (fImpl != NULL)
		fImpl->detachedOnly = detachedOnly;
}


void
BMediaUnit::SetDrivingInput(BMediaInput* input)
{
	if (fImpl == NULL)
		return;
	BAutolock _(fImpl->detachLock);
	fImpl->drivingInput = input;
}


status_t
BMediaUnit::_EnterDetached()
{
	if (fImpl == NULL)
		return B_NO_INIT;
	BAutolock _(fImpl->detachLock);
	if (fImpl->detachedOnly || fImpl->detachedMode)
		return B_OK;
	// A graph node (or a unit with a pw_filter) cannot change mode.
	if (fImpl->filter != NULL || IsStarted())
		return B_NOT_ALLOWED;
	fImpl->detachedMode = true;
	return B_OK;
}


void
BMediaUnit::_DetachedLink(BMediaConnection* connection, bool attach)
{
	if (fImpl == NULL || connection == NULL)
		return;
	BAutolock _(fImpl->detachLock);
	if (attach) {
		connection->fDetachedUnit = this;
		fImpl->detachedLinks++;
		if (fImpl->_Detached(connection) == NULL) {
			Impl::DetachedBuffer fresh;
			fresh.connection = connection;
			fresh.data = NULL;
			fresh.size = 0;
			fImpl->detachedBuffers.push_back(fresh);
		}
		return;
	}

	connection->fDetachedUnit = NULL;
	for (size_t i = 0; i < fImpl->detachedBuffers.size(); i++) {
		if (fImpl->detachedBuffers[i].connection == connection) {
			fImpl->detachedBuffers.erase(fImpl->detachedBuffers.begin() + i);
			break;
		}
	}
	if (fImpl->detachedLinks > 0)
		fImpl->detachedLinks--;
	if (fImpl->detachedLinks == 0 && !fImpl->detachedOnly && !IsStarted())
		fImpl->detachedMode = false;
}


status_t
BMediaUnit::_DetachedDeliver(BMediaInput* input, void* buffer, size_t size,
	const BMediaFormat& format)
{
	if (fImpl == NULL || input == NULL)
		return B_BAD_VALUE;

	// MIDI is handed over as it arrives, ahead of any cycle.
	if ((input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) != 0) {
		input->HandleBuffer(buffer, size, format);
		return B_OK;
	}

	BAutolock _(fImpl->detachLock);
	if (!IsStarted())
		return B_OK;

	if (fImpl->drivingInput == NULL) {
		for (int32 i = 0; i < CountInputs(); i++) {
			BMediaInput* candidate = InputAt(i);
			if (candidate != NULL && (candidate->AcceptedTypes()
					& B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) == 0) {
				fImpl->drivingInput = candidate;
				break;
			}
		}
	}

	Impl::DetachedBuffer* slot = fImpl->_Detached(input);
	if (slot == NULL)
		return B_ERROR;

	if (input != fImpl->drivingInput) {
		// Latch a copy: the sender's buffer is gone after it returns.
		slot->storage.assign((const uint8*)buffer, (const uint8*)buffer + size);
		slot->data = slot->storage.data();
		slot->size = size;
		return B_OK;
	}

	slot->data = buffer;
	slot->size = size;

	if (format.IsRawAudio()) {
		fImpl->quantum = (uint32)(size / sizeof(float));
		if (format.format.u.raw_audio.frame_rate > 0)
			fImpl->sampleRate = (uint32)format.format.u.raw_audio.frame_rate;
	} else {
		fImpl->quantum = (uint32)size;
	}
	fImpl->cycleStartTime = system_time();

	// Output buffers are sized to the cycle by _DetachedBufferFor().
	for (size_t i = 0; i < fImpl->detachedBuffers.size(); i++) {
		Impl::DetachedBuffer& out = fImpl->detachedBuffers[i];
		if (dynamic_cast<BMediaOutput*>(out.connection) != NULL) {
			out.storage.resize(size);
			out.data = out.storage.data();
			out.size = size;
		}
	}

	ProcessCycle(fImpl->quantum);

	status_t result = B_OK;
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL || output->fDetachedUnit != this
				|| !output->HasBinding()
				|| (output->AcceptedTypes()
					& B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) != 0)
			continue;
		Impl::DetachedBuffer* out = fImpl->_Detached(output);
		if (out == NULL || out->data == NULL)
			continue;
		status_t sent = output->SendBuffer(out->data, out->size);
		if (sent != B_OK)
			result = sent;
	}
	return result;
}


void*
BMediaUnit::_DetachedBufferFor(BMediaConnection* connection, size_t* outSize)
{
	// Called from ProcessCycle() on the sender's thread, lock already held.
	BAutolock _(fImpl->detachLock);
	Impl::DetachedBuffer* slot = fImpl->_Detached(connection);
	if (slot == NULL || slot->data == NULL) {
		if (outSize != NULL)
			*outSize = 0;
		return NULL;
	}
	if (outSize != NULL)
		*outSize = slot->size;
	return slot->data;
}
