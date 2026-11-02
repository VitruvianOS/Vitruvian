/*
 * Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "EventStream.h"

#include <InputServerTypes.h>
#include <ServerProtocol.h>
#include <shared_cursor_area.h>

#include <AppMisc.h>
#include <AutoDeleter.h>

#include <new>
#include <stdio.h>
#include <string.h>


EventStream::EventStream()
{
}


EventStream::~EventStream()
{
}


bool
EventStream::SupportsCursorThread() const
{
	return false;
}


status_t
EventStream::GetNextCursorPosition(BPoint& where, bigtime_t timeout)
{
	return B_ERROR;
}


//	#pragma mark -


InputServerStream::InputServerStream(BMessenger& messenger)
	:
	fInputServer(messenger),
	fPort(-1),
	fQuitting(false),
	fCursorArea(-1),
	fCursorBuffer(NULL),
	fLatestMouseMoved(NULL)
{
	BMessage message(IS_ACQUIRE_INPUT);
	message.AddInt32("remote team", BPrivate::current_team());

	fCursorArea = create_area("shared cursor", (void **)&fCursorBuffer, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_LAZY_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (fCursorArea >= B_OK) {
		// The wake word lives at the end of the shared area.
		fCursorEvent.SetTo(&fCursorBuffer->event);
		message.AddInt32("cursor area", fCursorArea);
	} else {
		fCursorArea = -1;
		fCursorBuffer = NULL;
	}

	BMessage reply;
	if (messenger.SendMessage(&message, &reply) != B_OK)
		return;

	if (reply.FindInt32("event port", &fPort) != B_OK)
		fPort = -1;

	// Without the input_server mapping the cursor moves on the event path.
	bool cursorArea;
	if (fCursorArea >= 0
		&& reply.FindBool("cursor area", &cursorArea) != B_OK) {
		delete_area(fCursorArea);
		fCursorArea = -1;
		fCursorBuffer = NULL;
		fCursorEvent.SetTo(NULL);
	}
}


#if TEST_MODE
InputServerStream::InputServerStream()
	:
	fQuitting(false),
	fCursorArea(-1),
	fCursorBuffer(NULL),
	fLatestMouseMoved(NULL)
{
	fPort = find_port(SERVER_INPUT_PORT);
}
#endif


InputServerStream::~InputServerStream()
{
	delete_area(fCursorArea);
}


bool
InputServerStream::IsValid()
{
	port_info portInfo;
	if (fPort < B_OK || get_port_info(fPort, &portInfo) != B_OK)
		return false;

	return true;
}


void
InputServerStream::SendQuit()
{
	fQuitting = true;
	write_port(fPort, 'quit', NULL, 0);
	if (fCursorBuffer != NULL)
		fCursorEvent.Post();
		// Wakes the cursor thread out of its wait on shutdown.
}


void
InputServerStream::UpdateScreenBounds(BRect bounds)
{
	UpdateScreenBounds(bounds, 0);
}


void
InputServerStream::UpdateScreenBounds(BRect bounds, int32 orientation,
	int32 reflection)
{
	BMessage update(IS_SCREEN_BOUNDS_UPDATED);
	update.AddRect("screen_bounds", bounds);
	update.AddInt32("screen_orientation", orientation);
	update.AddInt32("screen_reflection", reflection);

	fInputServer.SendMessage(&update);
}


bool
InputServerStream::GetNextEvent(BMessage** _event)
{
	while (fEvents.IsEmpty()) {
		// wait for new events
		BMessage* event;
		status_t status = _MessageFromPort(&event);
		if (status == B_OK) {
			if (event->what == B_MOUSE_MOVED)
				fLatestMouseMoved = event;

			fEvents.AddMessage(event);
		} else if (status == B_BAD_PORT_ID) {
			// our port got deleted - the input_server must have died
			fPort = -1;
			return false;
		}

		int32 count = port_count(fPort);
		if (count > 0) {
			// empty port queue completely while we're at it
			for (int32 i = 0; i < count; i++) {
				if (_MessageFromPort(&event, 0) == B_OK) {
					if (event->what == B_MOUSE_MOVED)
						fLatestMouseMoved = event;
					fEvents.AddMessage(event);
				}
			}
		}
	}

	// there are items in our list, so just work through them

	*_event = fEvents.NextMessage();
	return true;
}


status_t
InputServerStream::GetNextCursorPosition(BPoint &where, bigtime_t timeout)
{
	if (fCursorBuffer == NULL)
		return B_ERROR;

	// Wait on the futex word the input_server posts. The wait is sliced
	// so a dead input_server is noticed through the event port.
	const bigtime_t kSlice = 500000;
	const bigtime_t start = system_time();

	status_t status;
	for (;;) {
		bigtime_t slice = kSlice;
		if (timeout != B_INFINITE_TIMEOUT) {
			bigtime_t remaining = timeout - (system_time() - start);
			if (remaining <= 0)
				return B_TIMED_OUT;
			if (remaining < slice)
				slice = remaining;
		}

		do {
			status = fCursorEvent.WaitWithTimeout(slice);
		} while (status == B_INTERRUPTED);

		if (status == B_OK)
			break;

		if (status != B_TIMED_OUT)
			return status;

		if (timeout != B_INFINITE_TIMEOUT
			&& system_time() - start >= timeout) {
			return B_TIMED_OUT;
		}

		// Timed out without a post. Check the input_server is still
		// connected before waiting again.
		port_info info;
		if (get_port_info(fPort, &info) != B_OK) {
			fPort = -1;
			return B_ERROR;
		}
	}

#ifdef __VOS__
	uint32 pos = atomic_get((int32*)&fCursorBuffer->pos);
#else
	uint32 pos = fCursorBuffer->pos;
#endif

	where.x = pos >> 16UL;
	where.y = pos & 0xffff;

	atomic_and(&fCursorBuffer->read, 0);
		// this tells the input_server that we've read the
		// cursor position and want to be notified if updated

	if (fQuitting) {
		fQuitting = false;
		return B_ERROR;
	}

	return B_OK;
}


status_t
InputServerStream::InsertEvent(BMessage* event)
{
	fEvents.AddMessage(event);
	status_t status = write_port_etc(fPort, 'insm', NULL, 0, B_RELATIVE_TIMEOUT,
		0);
	if (status == B_BAD_PORT_ID)
		return status;

	// If the port is full, we obviously don't care to report this, as we
	// already placed our message.
	return B_OK;
}


BMessage*
InputServerStream::PeekLatestMouseMoved()
{
	return fLatestMouseMoved;
}


status_t
InputServerStream::_MessageFromPort(BMessage** _message, bigtime_t timeout)
{
	uint8 *buffer = NULL;
	ssize_t bufferSize;

	// read message from port

	do {
		bufferSize = port_buffer_size_etc(fPort, B_RELATIVE_TIMEOUT, timeout);
	} while (bufferSize == B_INTERRUPTED);

	if (bufferSize < B_OK)
		return bufferSize;

	if (bufferSize > 0) {
		buffer = new (std::nothrow) uint8[bufferSize];
		if (buffer == NULL)
			return B_NO_MEMORY;
	}

	int32 code;
	bufferSize = read_port_etc(fPort, &code, buffer, bufferSize,
		B_RELATIVE_TIMEOUT, 0);
	if (bufferSize < B_OK) {
		delete[] buffer;
		return bufferSize;
	}

	if (code == 'quit') {
		// this will cause GetNextEvent() to return false
		return B_BAD_PORT_ID;
	}
	if (code == 'insm') {
		// a message has been inserted into our queue
		return B_INTERRUPTED;
	}

	// we have the message, now let's unflatten it

	ObjectDeleter<BMessage> message(new BMessage(code));
	if (!message.IsSet())
		return B_NO_MEMORY;

	if (buffer == NULL) {
		*_message = message.Detach();
		return B_OK;
	}

	status_t status = message->Unflatten((const char*)buffer);
	delete[] buffer;

	if (status != B_OK) {
		printf("Unflatten event failed: %s, port message code was: %" B_PRId32
			" - %c%c%c%c\n", strerror(status), code, (int8)(code >> 24),
			(int8)(code >> 16), (int8)(code >> 8), (int8)code);
		return status;
	}

	*_message = message.Detach();
	return B_OK;
}

