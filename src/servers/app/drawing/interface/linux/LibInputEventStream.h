/*
 * Copyright 2019, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */
#ifndef LIBINPUT_EVENT_STREAM_H
#define LIBINPUT_EVENT_STREAM_H

#include "EventStream.h"

#include <Locker.h>
#include <ObjectList.h>

#include <libinput.h>
#include <linux/input.h>


class LibInputEventStream : public EventStream {
public:
									LibInputEventStream(uint32 width, uint32 height);
	virtual							~LibInputEventStream();

	virtual	bool					IsValid() { return true; }
	virtual	void					SendQuit() {}

	virtual	void					UpdateScreenBounds(BRect bounds);
	virtual	bool					GetNextEvent(BMessage** _event);
	virtual	status_t				InsertEvent(BMessage* event);
	virtual	BMessage*				PeekLatestMouseMoved();

private:
			static void				_PollEventsThread(void* cookie);
			void					_PollEvents();
			void					_ScheduleEvent(libinput_event* ev);

			// Key mapping
			int32							_MapKeyCode(uint32 linuxKeyCode);
			void							_UpdateModifiers(uint32 keyCode, bool pressed);
			uint32							_GetCurrentModifiers() const;
			void							_AddKeyEvent(BMessage* message, uint32 what, int32 key,
										uint32 modifiers, int32 repeatCount = 1);

			BObjectList<BMessage>	fEventList;
			BLocker					fEventListLocker;
			sem_id					fEventNotification;
			bool					fWaitingOnEvent;
			BMessage*				fLatestMouseMovedEvent;

			BPoint					fMousePosition;
			uint32					fMouseButtons;
			uint32					fModifiers;
			uint32					fOldModifiers;

			// Keyboard state
			bool							fKeyStates[KEY_MAX];
			bool							fCapsLock;
			bool							fNumLock;
			bool							fScrollLock;

			volatile bool			fRunning;
			struct udev*			fUDevHandle;
			struct libinput*		fInputHandle;
			uint32					fWidth;
			uint32					fHeight;
};

#endif // LIBINPUT_EVENT_STREAM_H
