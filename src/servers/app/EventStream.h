/*
 * Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef EVENT_STREAM_H
#define EVENT_STREAM_H


#include <LinkReceiver.h>
#include <MessageQueue.h>
#include <Messenger.h>
#include <SharedEvent.h>


struct shared_cursor;


class EventStream {
	public:
		EventStream();
		virtual ~EventStream();

		virtual bool IsValid() = 0;
		virtual void SendQuit() = 0;

		virtual bool SupportsCursorThread() const;

		virtual void UpdateScreenBounds(BRect bounds) = 0;
		// orientation: 0..3, matching DRM_MODE_PANEL_ORIENTATION_*.
		// reflection: B_PANEL_REFLECTION_*. Not pure, so backends that
		// don't care about rotation or reflection need no change.
		virtual void UpdateScreenBounds(BRect bounds, int32 orientation,
				int32 reflection = 0)
				{ UpdateScreenBounds(bounds); }

		// Relays the seat pointer constraint (flags: 1 = confined to
		// rect, 2 = relative mode) to input_server; default is a no-op
		// for streams that do not reach the device add-ons.
		virtual void SetPointerConstraint(uint32 /*flags*/,
				const BRect& /*rect*/)
				{}

		virtual bool GetNextEvent(BMessage** _event) = 0;
		virtual status_t GetNextCursorPosition(BPoint& where,
				bigtime_t timeout = B_INFINITE_TIMEOUT);

		virtual status_t InsertEvent(BMessage* event) = 0;

		virtual BMessage* PeekLatestMouseMoved() = 0;

		virtual bool GetCurrentMouseState(BPoint& where, uint32& buttons) const
			{ return false; }
};


class InputServerStream : public EventStream {
	public:
		InputServerStream(BMessenger& inputServerMessenger);
#if TEST_MODE
		InputServerStream();
#endif

		virtual ~InputServerStream();

		virtual bool IsValid();
		virtual void SendQuit();

		virtual bool SupportsCursorThread() const { return fCursorBuffer != NULL; }

		virtual void UpdateScreenBounds(BRect bounds);
		virtual void UpdateScreenBounds(BRect bounds, int32 orientation,
				int32 reflection = 0);

		virtual void SetPointerConstraint(uint32 flags,
				const BRect& rect);

		virtual bool GetNextEvent(BMessage** _event);
		virtual status_t GetNextCursorPosition(BPoint& where,
				bigtime_t timeout = B_INFINITE_TIMEOUT);

		virtual status_t InsertEvent(BMessage* event);

		virtual BMessage* PeekLatestMouseMoved();

	private:
		status_t _MessageFromPort(BMessage** _message,
			bigtime_t timeout = B_INFINITE_TIMEOUT);

		BMessenger fInputServer;
		BMessageQueue fEvents;
		port_id	fPort;
		bool	fQuitting;
		BSharedEvent	fCursorEvent;
		area_id	fCursorArea;
		shared_cursor* fCursorBuffer;
		BMessage* fLatestMouseMoved;
};

#endif	/* EVENT_STREAM_H */
