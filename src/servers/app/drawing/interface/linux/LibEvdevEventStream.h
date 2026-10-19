/*
 * Copyright 2024-2026, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */
#ifndef LIBEVDEV_EVENT_STREAM_H
#define LIBEVDEV_EVENT_STREAM_H

#include "EventStream.h"

#include <Locker.h>
#include <ObjectList.h>

#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <vector>
#include <map>

extern "C" {
#include <libseat.h>
}


struct EvdevDevice {
	int					fd;
	int					seatDeviceId;
	struct libevdev*	evdev;
	const char*			path;
	bool				isKeyboard;
	bool				isMouse;
	bool				isPointer;  // Touchpad or other pointing device
};


class LibEvdevEventStream : public EventStream {
public:
								// orientation: DRM_MODE_PANEL_ORIENTATION_*,
								// -1 to derive it from VOS_PANEL_ORIENTATION.
								// reflection: B_PANEL_REFLECTION_*, -1 to
								// derive it from VOS_PANEL_REFLECTION.
								LibEvdevEventStream(uint32 width, uint32 height,
									struct libseat* seat,
									int32 orientation = -1,
									int32 reflection = -1);
	virtual						~LibEvdevEventStream();

	virtual	bool				IsValid() { return !fDevices.empty(); }
	virtual	void				SendQuit() { fRunning = false; }

	virtual	void				UpdateScreenBounds(BRect bounds);
	virtual	void				UpdateScreenBounds(BRect bounds,
									int32 orientation, int32 reflection = 0);
	virtual	bool				GetNextEvent(BMessage** _event);
	virtual	status_t			InsertEvent(BMessage* event);
	virtual	BMessage*			PeekLatestMouseMoved();

	// Session management for VT switching
	void						Suspend();
	void						Resume();

private:
	static	int32				_PollEventsThread(void* cookie);
			void				_PollEvents();

	// Device management
			bool				_OpenDevice(const char* path);
			void				_CloseDevice(EvdevDevice& dev);
			void				_ScanDevices();

	// Event processing
			void				_ProcessKeyEvent(EvdevDevice& dev, struct input_event& ev);
			void				_ProcessRelEvent(EvdevDevice& dev, struct input_event& ev);
			void				_RotatePoint(float u, float v,
									float& outU, float& outV) const;
			void				_ProcessAbsEvent(EvdevDevice& dev, struct input_event& ev);
			void				_ProcessButtonEvent(EvdevDevice& dev, struct input_event& ev);
			void				_FlushPendingEvents();

	// Key mapping
			int32				_MapCharacter(uint32 linuxKeyCode,
									bool shift, bool caps);
			void				_UpdateModifiers(uint32 keyCode, bool pressed);
			uint32				_GetCurrentModifiers() const;
			void				_SendKeyEvent(uint32 what, int32 key,
									int32 character, uint32 modifiers);
			void				_SendMouseEvent(uint32 what);

	// Event list
			BObjectList<BMessage>	fEventList;
			BLocker					fEventListLocker;
			sem_id					fEventNotification;
			bool					fWaitingOnEvent;
			BMessage*				fLatestMouseMovedEvent;

	// Mouse state
			BPoint					fMousePosition;
			BPoint					fPendingMouseDelta;
			// Last known normalized (0..1) position in physical panel
			// space; ABS_X/ABS_Y arrive as separate events, so each one
			// re-rotates using the other axis' last known value.
			float					fLastAbsU, fLastAbsV;
			uint32					fMouseButtons;
			uint32					fModifiers;
			uint32					fOldModifiers;
			bool					fMouseMoved;

	// Keyboard state
			bool					fKeyStates[KEY_MAX];
			bool					fCapsLock;
			bool					fNumLock;
			bool					fScrollLock;

	// Runtime state
			volatile bool			fRunning;
			volatile bool			fSuspended;
			uint32					fWidth;
			uint32					fHeight;
			int32					fOrientation;
			int32					fReflection;

	// libseat integration
			struct libseat*			fSeat;
			std::vector<EvdevDevice> fDevices;
			int						fEpollFd;
};

#endif // LIBEVDEV_EVENT_STREAM_H
