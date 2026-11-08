/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "OutputAddonClient.h"

#include <AppMisc.h>
#include <Message.h>
#include <MessagePrivate.h>
#include <TokenSpace.h>

#include <new>
#include <string.h>
#include <unistd.h>


OutputAddonClient::OutputAddonClient()
	:
	fInitStatus(B_NO_INIT),
	fControlPort(-1),
	fReplyPort(-1),
	fUpdatePort(-1),
	fEventPort(-1),
	fServerTeam(-1),
	fTeam(-1),
	fScreen(-1),
	fArea(-1),
	fAreaBase(NULL),
	fHeader(NULL),
	fBytesPerRow(0),
	fWidth(0),
	fHeight(0)
{
	memset(&fLastFrame, 0, sizeof(fLastFrame));
	memset(fLastRects, 0, sizeof(fLastRects));
}


OutputAddonClient::~OutputAddonClient()
{
	Disconnect();
}


status_t
OutputAddonClient::InitCheck() const
{
	return fInitStatus;
}


status_t
OutputAddonClient::Connect(const char* name)
{
	if (fInitStatus == B_OK)
		return B_OK;

	fControlPort = find_port(OUTPUT_ADDON_PORT_NAME);
	if (fControlPort < B_OK)
		return fInitStatus = fControlPort;

	fReplyPort = create_port(8, "output addon client reply");
	if (fReplyPort < B_OK)
		return fInitStatus = fReplyPort;

	fTeam = (int32)BPrivate::current_team();

	BMessage request(kMsgOutputAddonRegister);
	request.AddInt32("team", fTeam);
	request.AddString("name", name != NULL ? name : "output add-on");

	BMessage reply;
	status_t status = _Request(request, &reply);
	if (status != B_OK)
		return fInitStatus = status;

	fServerTeam = reply.GetInt32("server team", -1);
	int32 protocol = reply.GetInt32("protocol", 0);
	if (protocol != kOutputAddonProtocolVersion)
		return fInitStatus = B_MISMATCHED_VALUES;

	return fInitStatus = B_OK;
}


void
OutputAddonClient::Disconnect()
{
	if (fScreen >= 0)
		DetachOutput();

	if (fControlPort >= B_OK && fTeam >= 0) {
		BMessage request(kMsgOutputAddonQuit);
		request.AddInt32("team", fTeam);
		BMessage reply;
		_Request(request, &reply);
	}

	if (fReplyPort >= B_OK) {
		delete_port(fReplyPort);
		fReplyPort = -1;
	}

	if (fUpdatePort >= B_OK) {
		delete_port(fUpdatePort);
		fUpdatePort = -1;
	}

	if (fEventPort >= B_OK) {
		delete_port(fEventPort);
		fEventPort = -1;
	}

	if (fArea >= B_OK) {
		delete_area(fArea);
		fArea = -1;
		fAreaBase = NULL;
		fHeader = NULL;
	}

	fControlPort = -1;
	fScreen = -1;
	fServerTeam = -1;
	fInitStatus = B_NO_INIT;
}


status_t
OutputAddonClient::_Request(BMessage& request, BMessage* reply)
{
	request.AddInt32("reply port", fReplyPort);
	// A raw port write has no target, the server looper would drop it.
	BMessage::Private(request).SetTarget(B_PREFERRED_TOKEN);

	ssize_t size = request.FlattenedSize();
	if (size <= 0)
		return B_BAD_VALUE;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return B_NO_MEMORY;

	if (request.Flatten(flat, size) != B_OK) {
		delete[] flat;
		return B_BAD_VALUE;
	}

	status_t status = write_port(fControlPort, request.what, flat, size);
	delete[] flat;
	if (status != B_OK)
		return status;

	uint8 buffer[2048];
	ssize_t readSize;
	int32 code;
	do {
		readSize = read_port_etc(fReplyPort, &code, buffer, sizeof(buffer),
			B_RELATIVE_TIMEOUT, 5 * 1000000);
	} while (readSize == B_INTERRUPTED);

	if (readSize < B_OK)
		return readSize;

	BMessage answer;
	if (answer.Unflatten((const char*)buffer) != B_OK)
		return B_BAD_VALUE;

	int32 result = answer.GetInt32("status", B_ERROR);
	if (reply != NULL)
		*reply = answer;

	return result;
}


status_t
OutputAddonClient::CreateVirtualOutput(int32 width, int32 height,
	const char* name, bool testPattern, int32* screen)
{
	if (fInitStatus != B_OK)
		return B_NO_INIT;

	BMessage request(kMsgOutputAddonCreateVirtual);
	request.AddInt32("width", width);
	request.AddInt32("height", height);
	request.AddBool("test pattern", testPattern);
	if (name != NULL)
		request.AddString("name", name);

	BMessage reply;
	status_t status = _Request(request, &reply);
	if (status != B_OK)
		return status;

	if (screen != NULL)
		*screen = reply.GetInt32("screen", -1);

	return B_OK;
}


status_t
OutputAddonClient::DeleteVirtualOutput(int32 screen)
{
	if (fInitStatus != B_OK)
		return B_NO_INIT;

	BMessage request(kMsgOutputAddonDeleteVirtual);
	request.AddInt32("screen", screen);

	return _Request(request, NULL);
}


status_t
OutputAddonClient::AttachOutput(int32 screen, bool wantInputEvents)
{
	if (fInitStatus != B_OK)
		return B_NO_INIT;
	if (fScreen >= 0)
		return B_BAD_VALUE;

	fUpdatePort = create_port(32, "output addon frames");
	if (fUpdatePort < B_OK)
		return fUpdatePort;

	if (wantInputEvents) {
		fEventPort = create_port(64, "output addon input");
		if (fEventPort < B_OK)
			return fEventPort;
	}

	BMessage request(kMsgOutputAddonAttach);
	request.AddInt32("team", fTeam);
	request.AddInt32("screen", screen);
	request.AddInt32("transport", kOutputAddonTransportSharedDamage);
	request.AddInt32("update port", fUpdatePort);
	if (fEventPort >= B_OK)
		request.AddInt32("event port", fEventPort);

	BMessage reply;
	status_t status = _Request(request, &reply);
	if (status != B_OK) {
		delete_port(fUpdatePort);
		fUpdatePort = -1;
		if (fEventPort >= B_OK) {
			delete_port(fEventPort);
			fEventPort = -1;
		}
		return status;
	}

	area_id source = reply.GetInt32("area", -1);
	int32 width = reply.GetInt32("width", 0);
	int32 height = reply.GetInt32("height", 0);
	int32 bytesPerRow = reply.GetInt32("bytes_per_row", 0);

	status = B_OK;
	if (source < B_OK || width <= 0 || height <= 0 || bytesPerRow <= 0)
		status = B_NO_INIT;

	if (status == B_OK) {
		// Returns the new area id, not a status.
		area_id clone = clone_area("output addon frame", &fAreaBase,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, source);
		if (clone < B_OK)
			status = clone;
		else
			fArea = clone;
	}

	if (status == B_OK) {
		fHeader = (output_addon_fb_header*)fAreaBase;
		if (fHeader->magic != OUTPUT_ADDON_MAGIC)
			status = B_BAD_DATA;
	}

	if (status != B_OK) {
		BMessage detach(kMsgOutputAddonDetach);
		detach.AddInt32("team", fTeam);
		detach.AddInt32("screen", screen);
		_Request(detach, NULL);

		if (fArea >= B_OK)
			delete_area(fArea);
		fArea = -1;
		fAreaBase = NULL;
		fHeader = NULL;
		delete_port(fUpdatePort);
		fUpdatePort = -1;
		if (fEventPort >= B_OK) {
			delete_port(fEventPort);
			fEventPort = -1;
		}
		return status;
	}

	fWidth = width;
	fHeight = height;
	fBytesPerRow = bytesPerRow;
	fScreen = screen;
	return B_OK;
}


status_t
OutputAddonClient::DetachOutput()
{
	if (fScreen < 0)
		return B_BAD_VALUE;

	BMessage request(kMsgOutputAddonDetach);
	request.AddInt32("team", fTeam);
	request.AddInt32("screen", fScreen);
	_Request(request, NULL);

	if (fAreaBase != NULL) {
		delete_area(fArea);
		fArea = -1;
		fAreaBase = NULL;
		fHeader = NULL;
	}

	if (fUpdatePort >= B_OK) {
		delete_port(fUpdatePort);
		fUpdatePort = -1;
	}

	if (fEventPort >= B_OK) {
		delete_port(fEventPort);
		fEventPort = -1;
	}

	fScreen = -1;
	return B_OK;
}


status_t
OutputAddonClient::WaitForFrame(output_addon_frame* frame,
	output_addon_rect* rects, bigtime_t timeout)
{
	if (fScreen < 0)
		return B_NO_INIT;

	uint8 buffer[sizeof(output_addon_frame)
		+ kOutputAddonMaxRects * sizeof(output_addon_rect)];
	int32 code;
	ssize_t size;
	do {
		size = read_port_etc(fUpdatePort, &code, buffer, sizeof(buffer),
			B_RELATIVE_TIMEOUT, timeout);
	} while (size == B_INTERRUPTED);

	if (size == B_TIMED_OUT)
		return B_TIMED_OUT;
	if (size < B_OK)
		return size == B_BAD_PORT_ID ? B_BAD_PORT_ID : B_ERROR;

	if (code == kOutputAddonCodeOutputRemoved)
		return B_NO_INIT;
	if (code != kOutputAddonCodeFrame)
		return B_BAD_VALUE;

	const output_addon_rect* parsed = NULL;
	if (output_addon_frame_decode(buffer, (int32)size, &fLastFrame, &parsed)
			< 0) {
		return B_BAD_DATA;
	}

	if (rects != NULL && fLastFrame.rect_count > 0) {
		memcpy(fLastRects, parsed,
			fLastFrame.rect_count * sizeof(output_addon_rect));
		memcpy(rects, parsed,
			fLastFrame.rect_count * sizeof(output_addon_rect));
	}

	if (frame != NULL)
		*frame = fLastFrame;

	return B_OK;
}


status_t
OutputAddonClient::CopyFrame(uint8* dst, int32 dstBytesPerRow)
{
	if (fScreen < 0 || fHeader == NULL || dst == NULL)
		return B_NO_INIT;

	uint32 generation = 0;
	uint32 index = 0;
	output_addon_unpack(fHeader->front, &generation, &index);
	if (generation == 0 || generation != fLastFrame.generation)
		return B_BAD_VALUE;

	if (index >= (uint32)fHeader->buffer_count)
		return B_BAD_DATA;

	const uint8* slot = (const uint8*)output_addon_fb_buffer(fHeader,
		fBytesPerRow, fHeight, (int32)index);
	output_addon_copy_rect(dst, dstBytesPerRow, slot, fBytesPerRow,
		fLastRects, fLastFrame.rect_count);

	// publish the ack so app_server can reuse the slot
	output_addon_publish(&fHeader->acked, generation, index);
	return B_OK;
}


status_t
OutputAddonClient::InjectInput(const BMessage& event)
{
	if (fInitStatus != B_OK)
		return B_NO_INIT;

	ssize_t size = event.FlattenedSize();
	if (size <= 0)
		return B_BAD_VALUE;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return B_NO_MEMORY;

	if (event.Flatten(flat, size) != B_OK) {
		delete[] flat;
		return B_BAD_VALUE;
	}

	BMessage request(kMsgOutputAddonInjectInput);
	request.AddData("event", B_MESSAGE_TYPE, flat, size);
	delete[] flat;

	return _Request(request, NULL);
}


status_t
OutputAddonClient::WaitForInput(BMessage* event, bigtime_t timeout)
{
	if (fEventPort < B_OK)
		return B_NOT_ALLOWED;
	if (event == NULL)
		return B_BAD_VALUE;

	uint8 buffer[4096];
	int32 code;
	ssize_t size;
	do {
		size = read_port_etc(fEventPort, &code, buffer, sizeof(buffer),
			B_RELATIVE_TIMEOUT, timeout);
	} while (size == B_INTERRUPTED);

	if (size == B_TIMED_OUT)
		return B_TIMED_OUT;
	if (size < B_OK)
		return B_ERROR;
	if (code != kOutputAddonCodeInput)
		return B_BAD_VALUE;

	return event->Unflatten((const char*)buffer);
}
