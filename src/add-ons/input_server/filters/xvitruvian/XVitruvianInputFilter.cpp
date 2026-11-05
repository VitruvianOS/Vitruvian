/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Seat-stream tap for XVitruvian. The X server creates this port and
// reads it in its own poll loop; when no server listens, writes fail
// and the filter stays a no-op.

#include "XVitruvianInputFilter.h"

#include <string.h>

#include <new>

#include <Message.h>
#include <Input.h>
#include <OS.h>

#include <XVitruvianInputProtocol.h>


extern "C" BInputServerFilter*
instantiate_input_filter()
{
	return new(std::nothrow) XVitruvianInputFilter();
}


XVitruvianInputFilter::XVitruvianInputFilter()
	:
	fPort(-1),
	fSeenCount(0)
{
}


status_t
XVitruvianInputFilter::InitCheck()
{
	return B_OK;
}


port_id
XVitruvianInputFilter::_InputPort()
{
	if (fPort >= B_OK)
		return fPort;

	fPort = find_port(XVITRUVIAN_INPUT_PORT_NAME);
	return fPort;
}


void
XVitruvianInputFilter::_WriteFlat(port_id port, BMessage* message)
{
	ssize_t size = message->FlattenedSize();
	if (size <= 0)
		return;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return;

	status_t status = message->Flatten(flat, size);
	if (status == B_OK) {
		// A wedged X server must never stall the seat: drop on a full
		// queue, same tradeoff as the output add-on input lane.
		status = write_port_etc(port, 0, flat, size, B_RELATIVE_TIMEOUT, 0);
		if (status == B_BAD_PORT_ID || status == B_BAD_VALUE)
			fPort = -1;
	}

	delete[] flat;
}


void
XVitruvianInputFilter::_NoteDevices(BMessage* message)
{
	int32 id;
	if (message->FindInt32("be:device_id", &id) != B_OK)
		return;

	for (int32 i = 0; i < fSeenCount; i++) {
		if (fSeen[i].id == id)
			return;
	}
	if (fSeenCount >= kMaxSeenDevices)
		return;

	SeenDevice& seen = fSeen[fSeenCount++];
	seen.id = id;
	seen.subtype = B_UNKNOWN_POINTING_DEVICE;
	seen.flags = 0;
	seen.name[0] = '\0';
	const char* name;
	if (message->FindString("be:device_name", &name) == B_OK)
		strlcpy(seen.name, name, sizeof(seen.name));
	message->FindInt32("be:device_subtype", &seen.subtype);
	message->FindInt32(XV_FIELD_DEVICE_FLAGS, &seen.flags);

	port_id port = _InputPort();
	if (port < B_OK)
		return;

	BMessage added(kXVDeviceAdded);
	added.AddInt32("be:device_id", seen.id);
	added.AddString("be:device_name", seen.name);
	added.AddInt32("be:device_subtype", seen.subtype);
	added.AddInt32(XV_FIELD_DEVICE_FLAGS, seen.flags);
	added.AddInt64("when", system_time());
	_WriteFlat(port, &added);
}


filter_result
XVitruvianInputFilter::Filter(BMessage* message, BList* /*outList*/)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
		case B_MODIFIERS_CHANGED:
		case B_MOUSE_MOVED:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_WHEEL_CHANGED:
		case kXVTouchDown:
		case kXVTouchMove:
		case kXVTouchUp:
		case kXVTouchCancel:
			break;
		default:
			return B_DISPATCH_MESSAGE;
	}

	port_id port = _InputPort();
	if (port < B_OK)
		return B_DISPATCH_MESSAGE;

	_NoteDevices(message);
	_WriteFlat(port, message);
	return B_DISPATCH_MESSAGE;
}
