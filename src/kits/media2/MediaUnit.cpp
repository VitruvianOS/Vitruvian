/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaUnit.h>

#include <new>
#include <string.h>

#include <pipewire/filter.h>
#include <spa/param/audio/format.h>
#include <spa/pod/builder.h>

#include "PipeWireBackend.h"


using namespace BPrivate::media;


struct BMediaUnit::Impl {
	BMediaUnit*				owner;

	pw_filter*				filter;
	spa_hook				listener;
	uint32					quantum;
	uint32					sampleRate;
	bigtime_t				cycleStartTime;

	static const pw_filter_events kFilterEvents;
	static void _OnProcess(void* data, struct spa_io_position* position);
	static void _OnStateChanged(void* data, enum pw_filter_state old,
		enum pw_filter_state state, const char* error);
};


const pw_filter_events BMediaUnit::Impl::kFilterEvents = {
	PW_VERSION_FILTER_EVENTS,
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
	return BMediaClient::Start();

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
	if (fImpl == NULL || fImpl->filter == NULL)
		return B_OK;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	pw_filter_destroy(fImpl->filter);
	fImpl->filter = NULL;

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

	return BMediaClient::Stop();
}


void
BMediaUnit::ProcessCycle(uint32 frameCount)
{
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL)
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
	} else if (connection->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) {
		pw_buffer* pb = pw_filter_dequeue_buffer(pd);
		if (pb != NULL && pb->buffer != NULL && pb->buffer->datas != NULL
			&& pb->buffer->datas[0].data != NULL) {
			if (outSize != NULL)
				*outSize = pb->buffer->datas[0].chunk->size;
			return pb->buffer->datas[0].data;
		}
	}

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

	impl->owner->ProcessCycle(impl->quantum);

	for (int32 i = 0; i < impl->owner->CountInputs(); i++) {
		BMediaInput* input = impl->owner->InputAt(i);
		if (input != NULL && input->_GetFilterPort() != NULL) {
			port_data* pd = (port_data*)input->_GetFilterPort();
			if (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) {
				pw_buffer* pb = pw_filter_dequeue_buffer(pd);
				if (pb != NULL)
					pw_filter_queue_buffer(pd, pb);
			}
		}
	}

	for (int32 i = 0; i < impl->owner->CountOutputs(); i++) {
		BMediaOutput* output = impl->owner->OutputAt(i);
		if (output != NULL && output->_GetFilterPort() != NULL) {
			port_data* pd = (port_data*)output->_GetFilterPort();
			if (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)) {
				pw_buffer* pb = pw_filter_dequeue_buffer(pd);
				if (pb != NULL)
					pw_filter_queue_buffer(pd, pb);
			}
		}
	}
}


void
BMediaUnit::Impl::_OnStateChanged(void* data, enum pw_filter_state,
	enum pw_filter_state state, const char*)
{
	BMediaUnit::Impl* impl = (BMediaUnit::Impl*)data;
	if (impl == NULL || impl->owner == NULL)
		return;

	if (state == PW_FILTER_STATE_UNCONNECTED) {
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
	}
}


void*
BMediaUnit::_GetFilter() const
{
	return fImpl != NULL ? fImpl->filter : NULL;
}
