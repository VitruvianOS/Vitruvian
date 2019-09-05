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
									LibInputEventStream();
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

			BObjectList<BMessage>	fEventList;
			BLocker					fEventListLocker;
			sem_id					fEventNotification;
			bool					fWaitingOnEvent;
			BMessage*				fLatestMouseMovedEvent;

			BPoint					fMousePosition;
			uint32					fMouseButtons;
			uint32					fModifiers;

			volatile bool			fRunning;
			struct udev*			fUDevHandle;
			struct libinput*		fInputHandle;
};

#endif // LIBINPUT_EVENT_STREAM_H
