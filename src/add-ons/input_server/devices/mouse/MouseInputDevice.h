/*
 * Copyright 2004-2025, Haiku.
 * Copyright 2026, The Vitruvian Project
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 *
 * Authors:
 *		Stefano Ceccherini
 *		Dario Casalinuovo
 */
#ifndef MOUSE_INPUT_DEVICE_H
#define MOUSE_INPUT_DEVICE_H


#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Point.h>
#include <Rect.h>

#include <ObjectList.h>


class MouseDevice;

class MouseInputDevice : public BInputServerDevice {
public:
							MouseInputDevice();
	virtual					~MouseInputDevice();

	virtual status_t		InitCheck();

	virtual status_t		Start(const char* name, void* cookie);
	virtual status_t		Stop(const char* name, void* cookie);

	virtual status_t		Control(const char* name, void* cookie,
								uint32 command, BMessage* message);

private:
	friend class MouseDevice;
	// TODO: needed by the control thread to remove a dead device
	// find a better way...

			status_t		_HandleMonitor(BMessage* message);
			status_t		_UpdateScreenBounds(MouseDevice* device,
								BMessage* message);
			void			_RecursiveScan(const char* directory);
			// Reconcile fDevices with /dev/input after resume; replaces
			// same-path nodes whose inode changed and drops stale ones.
			void			_RescanDevices();

			MouseDevice*	_FindDevice(const char* path) const;
			status_t		_AddDevice(const char* path);
			status_t		_RemoveDevice(const char* path);
			// Identity-aware form for the self-removal path: only removes
			// the device at `path` if it is still the one carrying
			// `serial`. See MouseDevice::fSerial.
			status_t		_RemoveDevice(const char* path, int32 serial);
			// Finds, ABA-checks (if serial != NULL) and detaches the
			// device from fDevices, all under one lock acquisition, but
			// does NOT delete it: the caller must delete the returned
			// pointer outside the lock, see _RemoveDevice() in the .cpp.
			MouseDevice*	_DetachDevice(const char* path,
								const int32* serial);

private:
			BObjectList<MouseDevice, true> fDevices;
			BLocker			fDeviceListLock;

public:
	// Shared cursor state — all MouseDevice instances read/write this under
	// fCursorLock so relative and absolute devices never fight over position.
			BPoint			fCursorPosition;	// (-1,-1) = not yet initialised
			BLocker			fCursorLock;
			// Union of buttons across devices; a seat can split motion
			// and clicks across two (e.g. VirtualBox's integration device).
			uint32			fButtons;
			BRect			fScreenFrame;
			int32			fOrientation;
			int32			fReflection;
};

extern "C" BInputServerDevice* instantiate_input_device();

#endif	// MOUSE_INPUT_DEVICE_H
