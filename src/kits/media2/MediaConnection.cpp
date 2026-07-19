/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaConnection.h>

#include <atomic>


static std::atomic<media_connection_id> sNextId{1};


// #pragma mark - BMediaConnection


BMediaConnection::BMediaConnection(media_connection_kinds kinds, const char* name)
	:
	fId(sNextId.fetch_add(1)),
	fName(name != NULL ? name : ""),
	fKinds(kinds),
	fOwner(NULL),
	fBinding(NULL),
	fBufferSize(0),
	fConnected(false)
{
}


BMediaConnection::~BMediaConnection()
{
}


media_connection_id
BMediaConnection::Id() const
{
	return fId;
}


const char*
BMediaConnection::Name() const
{
	return fName.String();
}


BMediaClient*
BMediaConnection::Client() const
{
	return fOwner;
}


bool
BMediaConnection::IsConnected() const
{
	return fConnected;
}


bool
BMediaConnection::HasBinding() const
{
	return fBinding != NULL;
}


BMediaConnection*
BMediaConnection::Binding() const
{
	return fBinding;
}


const BMediaFormat&
BMediaConnection::Format() const
{
	return fFormat;
}


status_t
BMediaConnection::SetAcceptedFormat(const BMediaFormat& format)
{
	fAcceptedFormat = format;
	return B_OK;
}


status_t
BMediaConnection::Disconnect()
{
	// Filled in when BMediaClient/BMediaGraph land.
	return B_NOT_SUPPORTED;
}


status_t
BMediaConnection::Release()
{
	delete this;
	return B_OK;
}


void
BMediaConnection::SetBufferSize(size_t size)
{
	fBufferSize = size;
}


void
BMediaConnection::Connected(const BMediaFormat& format)
{
	fFormat = format;
	fConnected = true;
}


void
BMediaConnection::Disconnected()
{
	fConnected = false;
}


// #pragma mark - BMediaInput


BMediaInput::BMediaInput(const char* name)
	:
	BMediaConnection(B_MEDIA_INPUT, name)
{
}


BMediaInput::~BMediaInput()
{
}


size_t
BMediaInput::BufferSize() const
{
	return BMediaConnection::Format().format.u.raw_audio.buffer_size;
}


void
BMediaInput::HandleBuffer(void* /*buffer*/, size_t /*size*/,
	const BMediaFormat& /*format*/)
{
}


void
BMediaInput::Connected(const BMediaFormat& format)
{
	BMediaConnection::Connected(format);
}


void
BMediaInput::Disconnected()
{
	BMediaConnection::Disconnected();
}


// #pragma mark - BMediaOutput


BMediaOutput::BMediaOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT, name),
	fEnabled(true),
	fFramesSent(0)
{
}


BMediaOutput::~BMediaOutput()
{
}


size_t
BMediaOutput::BufferSize() const
{
	return BMediaConnection::Format().format.u.raw_audio.buffer_size;
}


status_t
BMediaOutput::SendBuffer(void* /*buffer*/, size_t /*size*/)
{
	// Filled in once pw_filter port handling lands.
	return B_NOT_SUPPORTED;
}


void
BMediaOutput::Connected(const BMediaFormat& format)
{
	BMediaConnection::Connected(format);
}


void
BMediaOutput::Disconnected()
{
	BMediaConnection::Disconnected();
}
