/*
 * Copyright 2004-2006, Jérôme Duval. All rights reserved.
 * Copyright 2005-2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2008-2009, Stephan Aßmus, superstippi@gmx.de.
 * Copyright 2026, Dario Casalinuovo, superstippi@gmx.de.
 *
 * Distributed under the terms of the GPL License.
 */

#ifndef KEYBOARD_INPUT_DEVICE_H
#define KEYBOARD_INPUT_DEVICE_H


#include <Handler.h>
#include <InputServerDevice.h>
#include <Locker.h>
#include <String.h>

struct libevdev;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

#include <InputServerTypes.h>
#include <ObjectList.h>

#include "Keymap.h"
#include "TeamMonitorWindow.h"
#include "kb_mouse_settings.h"


class KeyboardInputDevice;

class KeyboardDevice : public BHandler {
public:
								KeyboardDevice(KeyboardInputDevice* owner,
									const char* path);
	virtual						~KeyboardDevice();

	virtual	void				MessageReceived(BMessage* message);
			status_t			Start();
			void				Stop();

			status_t			UpdateSettings(uint32 opcode = 0);
			void				HandleSeatMessage(uint32 what);

			const char*			Path() const { return fPath; }
			input_device_ref*	DeviceRef() { return &fDeviceRef; }

			int32				Serial() const { return fSerial; }

			status_t			GetDescription(BMessage* message) const;
			void				SetDescription(const char* name)
									{ fDescription = name; }

private:
	static	int32				_ControlThreadEntry(void* arg);
			int32				_ControlThread();
			void				_ControlThreadCleanup();
			void				_UpdateSettings(uint32 pending);
			void				_RebuildXkb();
			void				_SyncLocksFromLEDs();
			void				_ReleaseHeldKeys(uint8* states,
										bool& vtLCtrl, bool& vtRCtrl,
										bool& vtAlt, bool& vtRalt,
										bool& menuKeyDown,
										bool& ctrlAltDelPressed);
			void				_UpdateLEDs();
			status_t			_EnqueueInlineInputMethod(int32 opcode,
									const char* string = NULL,
									bool confirmed = false,
									BMessage* keyDown = NULL);

private:
			KeyboardInputDevice* fOwner;
			input_device_ref	fDeviceRef;
			char				fPath[B_PATH_NAME_LENGTH];
			int					fFD;
			int32				fSerial;
			BString				fDescription;
			struct libevdev*	fInputHandle;
			int					fEpollFd;
			struct xkb_context*	fXkbContext;
			struct xkb_keymap*	fXkbKeymap;
			struct xkb_state*	fXkbState;
			struct xkb_compose_table*	fXkbComposeTable;
			struct xkb_compose_state*	fXkbComposeState;
			thread_id			fThread;
			kb_settings			fSettings;
	volatile bool				fActive;
	volatile bool				fInputMethodStarted;
			uint32				fModifiers;
			uint32				fCommandKey;
			uint32				fControlKey;
			uint16				fKeyboardID;

			// Claimed with atomic_get_and_set(), so int32 and not
			// volatile: the atomic supplies the ordering, and a
			// volatile-qualified uint32 only type-checks under
			// -fpermissive.
			int32				fSettingsCommand;

			// Pending B_SEAT_DISABLED, B_SEAT_ENABLED or B_SYSTEM_RESUMED, 0 = none. A single command,
			// not a bitmask like fSettingsCommand, since these are mutually exclusive.
			int32				fSeatCommand;

			Keymap				fKeymap;
			BLocker				fKeymapLock;
};


class KeyboardInputDevice : public BInputServerDevice {
public:
								KeyboardInputDevice();
	virtual						~KeyboardInputDevice();

	virtual	status_t			InitCheck();

	virtual	status_t			Start(const char* name, void* cookie);
	virtual status_t			Stop(const char* name, void* cookie);

	virtual status_t			Control(const char* name, void* cookie,
									uint32 command, BMessage* message);

	virtual status_t			SystemShuttingDown();

private:
	friend class KeyboardDevice;
	// TODO: needed by the control thread to remove a dead device
	// find a better way...

			status_t			_HandleMonitor(BMessage* message);
			void				_RecursiveScan(const char* directory);

			KeyboardDevice*		_FindDevice(const char* path) const;
			status_t			_AddDevice(const char* path);
			status_t			_RemoveDevice(const char* path);
			status_t			_RemoveDevice(const char* path, int32 serial);
			// Finds, ABA-checks (if serial != NULL) and detaches the
			// device from fDevices, all under one lock acquisition, but
			// does NOT delete it: the caller must delete the returned
			// pointer outside the lock, see _RemoveDevice() in the .cpp.
			KeyboardDevice*		_DetachDevice(const char* path,
									const int32* serial);

			BObjectList<KeyboardDevice, true> fDevices;
			BLocker				fDeviceListLock;
			TeamMonitorWindow*	fTeamMonitorWindow;
};

extern "C" BInputServerDevice* instantiate_input_device();

#endif	// KEYBOARD_INPUT_DEVICE_H
