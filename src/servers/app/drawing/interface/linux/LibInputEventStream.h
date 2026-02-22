/*
 * Copyright 2019-2026, Dario Casalinuovo
 * Distributed under the terms of the GPL License.
 */
#ifndef LIBINPUT_EVENT_STREAM_H
#define LIBINPUT_EVENT_STREAM_H

#include "EventStream.h"

#include <Locker.h>
#include <ObjectList.h>

#include <libinput.h>
#include <linux/input.h>
#include <map>
#include <mutex>

extern "C" {
#include <libseat.h>
}


class LibInputEventStream : public EventStream {
public:
									LibInputEventStream(uint32 width, uint32 height,
										struct libseat* seat = NULL);
	virtual							~LibInputEventStream();

	virtual	bool					IsValid() { return fInputHandle != NULL; }
	virtual	void					SendQuit() {}

	virtual	void					UpdateScreenBounds(BRect bounds);
	virtual	bool					GetNextEvent(BMessage** _event);
	virtual	status_t				InsertEvent(BMessage* event);
	virtual	BMessage*				PeekLatestMouseMoved();

			void					Suspend();
			void					Resume();

			void					SetSeat(struct libseat* seat) { fSeat = seat; }

			static	int				_OpenRestricted(const char* path, int flags, void* userData);
			static	void			_CloseRestricted(int fd, void* userData);

			void					SetSeatMutex(std::mutex* mutex)
									{
										fSeatMutex = mutex;
									}
private:
			static	void			_PollEventsThread(void* cookie);
			void					_PollEvents();
			void					_ScheduleEvent(libinput_event* ev);

			int32					_MapKeyCode(uint32 linuxKeyCode);
			void					_UpdateModifiers(uint32 keyCode, bool pressed);
			uint32					_GetCurrentModifiers() const;
			void					_AddKeyEvent(BMessage* message, uint32 what, int32 key,
										uint32 modifiers, int32 repeatCount = 1);

			BObjectList<BMessage, true>	fEventList;
			BLocker					fEventListLocker;
			sem_id					fEventNotification;
			bool					fWaitingOnEvent;
			BMessage*				fLatestMouseMovedEvent;

			BPoint					fMousePosition;
			uint32					fMouseButtons;
			uint32					fModifiers;
			uint32					fOldModifiers;

			bool					fKeyStates[KEY_MAX];
			bool					fCapsLock;
			bool					fNumLock;
			bool					fScrollLock;

			volatile bool			fRunning;
			volatile bool			fSuspended;
			struct udev*			fUDevHandle;
			struct libinput*		fInputHandle;
			uint32					fWidth;
			uint32					fHeight;

			struct libseat*			fSeat;
			std::map<int, int>		fDeviceIds;
			BLocker					fDeviceIdsLock;
			BLocker					fLibInputLock;

			bigtime_t				fLastResumeTime;
			static const bigtime_t	kVTSwitchCooldown = 500000;

			std::mutex*				fSeatMutex;
};


#endif // LIBINPUT_EVENT_STREAM_H
