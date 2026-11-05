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
#include <OS.h>


// Must match kXVitruvianInputPortName in the hw/vitruvian DDX.
#define XVITRUVIAN_INPUT_PORT_NAME	"v-os:xvitruvian_input"


extern "C" BInputServerFilter*
instantiate_input_filter()
{
	return new(std::nothrow) XVitruvianInputFilter();
}


XVitruvianInputFilter::XVitruvianInputFilter()
	:
	fPort(-1)
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
			break;
		default:
			return B_DISPATCH_MESSAGE;
	}

	port_id port = _InputPort();
	if (port < B_OK)
		return B_DISPATCH_MESSAGE;

	ssize_t size = message->FlattenedSize();
	if (size <= 0)
		return B_DISPATCH_MESSAGE;

	char* flat = new(std::nothrow) char[size];
	if (flat == NULL)
		return B_DISPATCH_MESSAGE;

	status_t status = message->Flatten(flat, size);
	if (status == B_OK) {
		// A wedged X server must never stall the seat: drop on a full
		// queue, same tradeoff as the output add-on input lane.
		status = write_port_etc(port, 0, flat, size, B_RELATIVE_TIMEOUT, 0);
		if (status == B_BAD_PORT_ID || status == B_BAD_VALUE)
			fPort = -1;
	}

	delete[] flat;
	return B_DISPATCH_MESSAGE;
}
