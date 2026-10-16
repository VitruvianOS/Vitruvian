/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _GAMEPAD_POINTER_DEVICE_H
#define _GAMEPAD_POINTER_DEVICE_H


#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Point.h>
#include <Rect.h>

#include <ObjectList.h>

#include <PointerControlCore.h>


class GamepadDevice;


// Converts a gamepad into the seat pointer and a few keys. Off by default.
class GamepadPointerDevice : public BInputServerDevice {
public:
							GamepadPointerDevice();
	virtual					~GamepadPointerDevice();

	virtual status_t		InitCheck();

	virtual status_t		Start(const char* name, void* cookie);
	virtual status_t		Stop(const char* name, void* cookie);

	virtual status_t		Control(const char* name, void* cookie,
								uint32 command, BMessage* message);

private:
	friend class GamepadDevice;

			status_t		_HandleMonitor(BMessage* message);
			status_t		_UpdateScreenBounds(GamepadDevice* device,
								BMessage* message);
			status_t		_UpdatePointerConstraint(GamepadDevice* device,
								BMessage* message);
			void			GetPointerConstraint(
								pointer_constraint* out) const;
			void			_RescanDevices();

			GamepadDevice*	_FindDevice(const char* path) const;
			status_t		_AddDevice(const char* path);
			status_t		_RemoveDevice(const char* path);
			status_t		_RemoveDevice(const char* path, int32 serial);
			GamepadDevice*	_DetachDevice(const char* path,
								const int32* serial);

			void			_LoadSettings();

private:
			BObjectList<GamepadDevice, true> fDevices;
			BLocker			fDeviceListLock;

			bool			fEnabledByDefault;
			float			fDeadzone;
			float			fSensitivity;

public:
			// Local estimate, drifts if a mouse moves at the same time.
			BPoint			fCursorPosition;
			BLocker			fCursorLock;
			uint32			fButtons;
			BRect			fScreenFrame;
			int32			fOrientation;
			int32			fReflection;

private:
			mutable BLocker	fConstraintLock;
			pointer_constraint fConstraint;
};


extern "C" BInputServerDevice* instantiate_input_device();

#endif	// _GAMEPAD_POINTER_DEVICE_H
