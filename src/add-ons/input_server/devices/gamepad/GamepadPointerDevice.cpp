/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Gamepad-to-pointer add-on (couch and lizard mode).

#include "GamepadPointerDevice.h"

#include <errno.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <Directory.h>
#include <OS.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Screen.h>
#include <String.h>

#include <algorithm>

// Used instead of <linux/input.h>: the legacy and Linux input headers
// share a guard.
#include "../LinuxEvdevShim.h"

#include <PanelOrientationTransform.h>
#include <PointerControlCore.h>
#include <XVitruvianInputProtocol.h>


static const char* kDevicesDirectory = "/dev/input";
static const char* kSettingsRelativePath = "input/gamepad_pointer";

// Legacy key codes.
static const int32 kKeyEnter = 0x47;
static const int32 kKeyUp = 0x57;
static const int32 kKeyLeft = 0x61;
static const int32 kKeyDown = 0x62;
static const int32 kKeyRight = 0x63;

static const uint32 kButtonLeft = 0x1;
static const uint32 kButtonRight = 0x2;
static const uint32 kButtonMiddle = 0x4;

// Pixels per event at full stick deflection, before sensitivity.
static const float kMaxSpeed = 24.0f;
// Right-stick units before one wheel step (axis range is ~32767).
static const int32 kWheelThreshold = 16000;


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) GamepadPointerDevice();
}


class GamepadDevice {
public:
								GamepadDevice(GamepadPointerDevice& target,
									const char* path);
								~GamepadDevice();

			status_t			Start();
			void				Stop();

			status_t			Classify();

			void				HandleSeatMessage(uint32 what);
			void				UpdateScreenBounds(BRect frame,
									int32 orientation, int32 reflection);

			const char*			Path() const { return fPath.String(); }
			int32				Serial() const { return fSerial; }
			ino_t				Inode() const { return fDeviceInode; }
			input_device_ref*	DeviceRef() { return &fDeviceRef; }

private:
	static	status_t			_ControlThreadEntry(void* arg);
			void				_ControlThread();
			void				_ControlThreadCleanup();

			void				_ApplyPointer(BPoint& point, float dx,
									float dy);
			void				_EmitMove(BPoint& where);
			void				_EmitButtons(uint32 buttons, BPoint& where);
			void				_EmitKey(int32 haikuKey, int32 evdevCode,
									bool down);
			void				_EmitWheel(float dx, float dy);

private:
			GamepadPointerDevice& fTarget;
			BString				fPath;
			int					fDevice;
			struct libevdev*	fEvdevHandle;
			int					fEpollFd;

			int32				fAbsMinX, fAbsMinY;
			int32				fAbsMaxX, fAbsMaxY;
			int32				fAbsMinRX, fAbsMinRY;
			int32				fAbsMaxRX, fAbsMaxRY;

			input_device_ref	fDeviceRef;
			int32				fSerial;
			ino_t				fDeviceInode;

			bool				fEnabled;
			bool				fFastMode;
			bool				fDpadUp, fDpadDown, fDpadLeft, fDpadRight;
			bool				fStartDown;
			float				fWheelAccX;
			float				fWheelAccY;

			thread_id			fThread;
	volatile bool				fActive;
};


static int32 sNextSerial = 0;


GamepadDevice::GamepadDevice(GamepadPointerDevice& target, const char* path)
	:
	fTarget(target),
	fPath(path),
	fDevice(-1),
	fEvdevHandle(NULL),
	fEpollFd(-1),
	fAbsMinX(0),
	fAbsMinY(0),
	fAbsMaxX(32767),
	fAbsMaxY(32767),
	fAbsMinRX(0),
	fAbsMinRY(0),
	fAbsMaxRX(32767),
	fAbsMaxRY(32767),
	fDeviceInode(0),
	fEnabled(false),
	fFastMode(false),
	fDpadUp(false),
	fDpadDown(false),
	fDpadLeft(false),
	fDpadRight(false),
	fStartDown(false),
	fWheelAccX(0),
	fWheelAccY(0),
	fThread(-1),
	fActive(false)
{
	fDeviceRef.name = NULL;
	fDeviceRef.type = B_POINTING_DEVICE;
	fDeviceRef.cookie = this;
	fSerial = atomic_add(&sNextSerial, 1);
}


GamepadDevice::~GamepadDevice()
{
	Stop();

	free(fDeviceRef.name);
	fDeviceRef.name = NULL;
}


status_t
GamepadDevice::Classify()
{
	int fd = open(fPath.String(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return (errno == EACCES || errno == EPERM)
			? B_PERMISSION_DENIED : B_ERROR;

	struct libevdev* evdev = NULL;
	if (libevdev_new_from_fd(fd, &evdev) < 0) {
		close(fd);
		return B_BAD_TYPE;
	}

	bool hasPadButton = libevdev_has_event_code(evdev, EV_KEY, BTN_GAMEPAD)
		|| libevdev_has_event_code(evdev, EV_KEY, BTN_JOYSTICK);
	bool hasStick = libevdev_has_event_code(evdev, EV_ABS, ABS_X)
		&& libevdev_has_event_code(evdev, EV_ABS, ABS_Y);
	if (!hasPadButton || !hasStick) {
		libevdev_free(evdev);
		close(fd);
		return B_BAD_TYPE;
	}

	// Skip Steam's virtual pad, its lizard mode already drives the cursor.
	const char* name = libevdev_get_name(evdev);
	if (name != NULL && strncmp(name, "Steam ", 6) == 0) {
		libevdev_free(evdev);
		close(fd);
		return B_BAD_TYPE;
	}

	fAbsMinX = libevdev_get_abs_minimum(evdev, ABS_X);
	fAbsMaxX = libevdev_get_abs_maximum(evdev, ABS_X);
	fAbsMinY = libevdev_get_abs_minimum(evdev, ABS_Y);
	fAbsMaxY = libevdev_get_abs_maximum(evdev, ABS_Y);
	if (fAbsMaxX <= fAbsMinX)
		fAbsMaxX = fAbsMinX + 32767;
	if (fAbsMaxY <= fAbsMinY)
		fAbsMaxY = fAbsMinY + 32767;

	fAbsMinRX = libevdev_get_abs_minimum(evdev, ABS_RX);
	fAbsMaxRX = libevdev_get_abs_maximum(evdev, ABS_RX);
	fAbsMinRY = libevdev_get_abs_minimum(evdev, ABS_RY);
	fAbsMaxRY = libevdev_get_abs_maximum(evdev, ABS_RY);
	if (fAbsMaxRX <= fAbsMinRX) {
		fAbsMinRX = 0;
		fAbsMaxRX = kWheelThreshold;
	}
	if (fAbsMaxRY <= fAbsMinRY) {
		fAbsMinRY = 0;
		fAbsMaxRY = kWheelThreshold;
	}

	struct stat st;
	if (fstat(fd, &st) == 0)
		fDeviceInode = st.st_ino;

	free(fDeviceRef.name);
	fDeviceRef.name = strdup(name != NULL && name[0] != '\0'
		? name : "Gamepad Pointer");

	libevdev_free(evdev);
	close(fd);
	return fDeviceRef.name != NULL ? B_OK : B_NO_MEMORY;
}


status_t
GamepadDevice::Start()
{
	if (fDevice >= 0)
		return B_OK;

	fDevice = open(fPath.String(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fDevice < 0)
		return B_ERROR;

	if (libevdev_new_from_fd(fDevice, &fEvdevHandle) < 0) {
		close(fDevice);
		fDevice = -1;
		return B_BAD_TYPE;
	}

	fEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (fEpollFd < 0) {
		libevdev_free(fEvdevHandle);
		fEvdevHandle = NULL;
		close(fDevice);
		fDevice = -1;
		return B_ERROR;
	}

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = fDevice;
	if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fDevice, &event) < 0) {
		close(fEpollFd);
		fEpollFd = -1;
		libevdev_free(fEvdevHandle);
		fEvdevHandle = NULL;
		close(fDevice);
		fDevice = -1;
		return B_ERROR;
	}

	fEnabled = fTarget.fEnabledByDefault;
	fActive = true;
	fThread = spawn_thread(_ControlThreadEntry, "gamepad pointer",
		B_NORMAL_PRIORITY, this);
	if (fThread < B_OK) {
		status_t error = fThread;
		fActive = false;
		fThread = -1;
		close(fEpollFd);
		fEpollFd = -1;
		libevdev_free(fEvdevHandle);
		fEvdevHandle = NULL;
		close(fDevice);
		fDevice = -1;
		return error;
	}

	resume_thread(fThread);
	return B_OK;
}


void
GamepadDevice::Stop()
{
	if (fThread < B_OK)
		return;

	fActive = false;

	// Self-removal must not join itself (input-server.md rule 2).
	thread_id thread = fThread;
	fThread = -1;
	if (thread != find_thread(NULL))
		wait_for_thread(thread, NULL);
}


int32
GamepadDevice::_ControlThreadEntry(void* arg)
{
	((GamepadDevice*)arg)->_ControlThread();
	return B_OK;
}


void
GamepadDevice::_ControlThread()
{
	while (fActive) {
		struct epoll_event fired;
		int n = epoll_wait(fEpollFd, &fired, 1, 100);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			continue;

		int32 stickX = 0, stickY = 0, wheelX = 0, wheelY = 0;
		uint32 buttons = 0;
		bool toggleMode = false;
		bool fast = fFastMode;
		bool emitAnything = false;

		struct input_event iev;
		for (;;) {
			int rc = libevdev_next_event(fEvdevHandle,
				LIBEVDEV_READ_FLAG_NORMAL, &iev);
			if (rc == -EAGAIN)
				break;
			if (rc < 0)
				break;
			if (iev.type == EV_ABS) {
				switch (iev.code) {
					case ABS_X: stickX = iev.value; break;
					case ABS_Y: stickY = iev.value; break;
					case ABS_RX: wheelX = iev.value; break;
					case ABS_RY: wheelY = iev.value; break;
					default: break;
				}
				emitAnything = true;
			} else if (iev.type == EV_KEY) {
				if (iev.code >= BTN_SOUTH && iev.code <= BTN_THUMBR) {
					int32 index = iev.code - BTN_SOUTH;
					if (iev.value)
						buttons |= 1u << index;
				}
				if (iev.code == BTN_MODE && iev.value == 1)
					toggleMode = true;
				if (iev.code == BTN_THUMBL)
					fast = iev.value != 0;
				emitAnything = true;
			} else if (iev.type == EV_SYN && iev.code == SYN_REPORT)
				break;
		}

		if (toggleMode)
			fEnabled = !fEnabled;
		fFastMode = fast;

		if (!fEnabled) {
			// Release whatever was held when the mode went off.
			if (fDpadUp) {
				_EmitKey(kKeyUp, BTN_DPAD_UP, false);
				fDpadUp = false;
			}
			if (fDpadDown) {
				_EmitKey(kKeyDown, BTN_DPAD_DOWN, false);
				fDpadDown = false;
			}
			if (fDpadLeft) {
				_EmitKey(kKeyLeft, BTN_DPAD_LEFT, false);
				fDpadLeft = false;
			}
			if (fDpadRight) {
				_EmitKey(kKeyRight, BTN_DPAD_RIGHT, false);
				fDpadRight = false;
			}
			if (fStartDown) {
				_EmitKey(kKeyEnter, BTN_START, false);
				fStartDown = false;
			}
			continue;
		}

		if (!emitAnything)
			continue;

		BPoint where;
		if (fTarget.fCursorLock.Lock()) {
			where = fTarget.fCursorPosition;
			fTarget.fCursorLock.Unlock();
		}

		// Left stick: deadzone, quadratic curve, speed scale.
		float centerX = (fAbsMinX + fAbsMaxX) / 2.0f;
		float centerY = (fAbsMinY + fAbsMaxY) / 2.0f;
		float rangeX = (fAbsMaxX - fAbsMinX) / 2.0f;
		float rangeY = (fAbsMaxY - fAbsMinY) / 2.0f;
		float deadzone = fTarget.fDeadzone;
		float unitX = rangeX > 0 ? ((float)stickX - centerX) / rangeX : 0;
		float unitY = rangeY > 0 ? ((float)stickY - centerY) / rangeY : 0;
		float speed = fTarget.fSensitivity * (fFastMode ? 2.0f : 1.0f);
		float dx = 0, dy = 0;
		if (unitX > deadzone || unitX < -deadzone) {
			float adjusted = (fabsf(unitX) - deadzone) / (1.0f - deadzone);
			dx = (unitX < 0 ? -1.0f : 1.0f) * adjusted * adjusted
				* kMaxSpeed * speed;
		}
		if (unitY > deadzone || unitY < -deadzone) {
			float adjusted = (fabsf(unitY) - deadzone) / (1.0f - deadzone);
			dy = (unitY < 0 ? -1.0f : 1.0f) * adjusted * adjusted
				* kMaxSpeed * speed;
		}

		// Right stick: wheel steps.
		float wheelRangeX = (fAbsMaxRX - fAbsMinRX) / 2.0f;
		float wheelRangeY = (fAbsMaxRY - fAbsMinRY) / 2.0f;
		float wheelCenterX = (fAbsMinRX + fAbsMaxRX) / 2.0f;
		float wheelCenterY = (fAbsMinRY + fAbsMaxRY) / 2.0f;
		fWheelAccX += wheelRangeX > 0
			? ((float)wheelX - wheelCenterX) / wheelRangeX : 0;
		fWheelAccY += wheelRangeY > 0
			? ((float)wheelY - wheelCenterY) / wheelRangeY : 0;
		float stepsX = 0, stepsY = 0;
		if (fWheelAccX > 1.0f || fWheelAccX < -1.0f) {
			stepsX = fWheelAccX > 0 ? 1.0f : -1.0f;
			fWheelAccX = 0;
		}
		if (fWheelAccY > 1.0f || fWheelAccY < -1.0f) {
			stepsY = fWheelAccY > 0 ? 1.0f : -1.0f;
			fWheelAccY = 0;
		}

		// Face buttons and Start/d-pad as keys.
		bool south = (buttons & (1u << (BTN_SOUTH - BTN_SOUTH))) != 0;
		bool east = (buttons & (1u << (BTN_EAST - BTN_SOUTH))) != 0;
		bool north = (buttons & (1u << (BTN_NORTH - BTN_SOUTH))) != 0;
		(void)south;
		(void)east;
		(void)north;

		uint32 mouseButtons = 0;
		if (south)
			mouseButtons |= kButtonLeft;
		if (east)
			mouseButtons |= kButtonRight;
		if (north)
			mouseButtons |= kButtonMiddle;

		if (dx != 0 || dy != 0)
			_ApplyPointer(where, dx, dy);

		if (dx != 0 || dy != 0 || mouseButtons != fTarget.fButtons)
			_EmitMove(where);
		_EmitButtons(mouseButtons, where);

		if (stepsX != 0 || stepsY != 0)
			_EmitWheel(stepsX, stepsY);

		bool dpadUp = libevdev_get_event_value(fEvdevHandle, EV_KEY,
			BTN_DPAD_UP) != 0;
		bool dpadDown = libevdev_get_event_value(fEvdevHandle, EV_KEY,
			BTN_DPAD_DOWN) != 0;
		bool dpadLeft = libevdev_get_event_value(fEvdevHandle, EV_KEY,
			BTN_DPAD_LEFT) != 0;
		bool dpadRight = libevdev_get_event_value(fEvdevHandle, EV_KEY,
			BTN_DPAD_RIGHT) != 0;
		bool start = libevdev_get_event_value(fEvdevHandle, EV_KEY,
			BTN_START) != 0;

		if (dpadUp != fDpadUp) {
			fDpadUp = dpadUp;
			_EmitKey(kKeyUp, BTN_DPAD_UP, dpadUp);
		}
		if (dpadDown != fDpadDown) {
			fDpadDown = dpadDown;
			_EmitKey(kKeyDown, BTN_DPAD_DOWN, dpadDown);
		}
		if (dpadLeft != fDpadLeft) {
			fDpadLeft = dpadLeft;
			_EmitKey(kKeyLeft, BTN_DPAD_LEFT, dpadLeft);
		}
		if (dpadRight != fDpadRight) {
			fDpadRight = dpadRight;
			_EmitKey(kKeyRight, BTN_DPAD_RIGHT, dpadRight);
		}
		if (start != fStartDown) {
			fStartDown = start;
			_EmitKey(kKeyEnter, BTN_START, start);
		}
	}

	_ControlThreadCleanup();
}


void
GamepadDevice::_ControlThreadCleanup()
{
	if (fEpollFd >= 0) {
		close(fEpollFd);
		fEpollFd = -1;
	}
	if (fEvdevHandle != NULL) {
		libevdev_free(fEvdevHandle);
		fEvdevHandle = NULL;
	}
	if (fDevice >= 0) {
		close(fDevice);
		fDevice = -1;
	}

	// _RemoveDevice deletes this object on this thread, so nothing below
	// may touch members.
	fThread = -1;
	BString path = fPath;
	fTarget._RemoveDevice(path.String(), fSerial);
}


void
GamepadDevice::_ApplyPointer(BPoint& point, float dx, float dy)
{
	pointer_constraint constraint;
	fTarget.GetPointerConstraint(&constraint);
	if ((constraint.flags & kPointerConstraintRelative) != 0) {
		float x = 0;
		float y = 0;
		pointer_constraint_move_relative(&constraint, dx, dy, &x, &y);
		point.x = x;
		point.y = y;
	} else {
		point.x += dx;
		point.y += dy;
		pointer_constraint_clamp(&constraint, &point.x, &point.y);
	}

	point.x = std::max(0.0f, std::min((float)(fTarget.fScreenFrame.right),
		point.x));
	point.y = std::max(0.0f, std::min((float)(fTarget.fScreenFrame.bottom),
		point.y));

	if (fTarget.fCursorLock.Lock()) {
		fTarget.fCursorPosition = point;
		fTarget.fCursorLock.Unlock();
	}
}


void
GamepadDevice::_EmitMove(BPoint& where)
{
	BMessage* message = new BMessage(B_MOUSE_MOVED);
	if (message == NULL)
		return;
	message->AddPoint("where", where);
	message->AddInt32("buttons", (int32)fTarget.fButtons);
	message->AddInt32("modifiers", 0);
	message->AddInt64("when", system_time());
	message->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
	message->AddInt32("be:device_id", fSerial);
	message->AddString("be:device_name", fDeviceRef.name);
	message->AddInt32(XV_FIELD_DEVICE_FLAGS,
		kXVDeviceHasPointer | kXVDeviceHasJoystick);
	fTarget.EnqueueMessage(message);
}


void
GamepadDevice::_EmitButtons(uint32 buttons, BPoint& where)
{
	uint32 changed = buttons ^ fTarget.fButtons;
	if (changed == 0)
		return;

	fTarget.fButtons = buttons;
	for (uint32 bit = 0; bit < 3; bit++) {
		uint32 mask = 1u << bit;
		if ((changed & mask) == 0)
			continue;

		BMessage* message = new BMessage((buttons & mask)
			? B_MOUSE_DOWN : B_MOUSE_UP);
		if (message == NULL)
			continue;
		message->AddPoint("where", where);
		message->AddInt32("buttons", (int32)buttons);
		message->AddInt32("modifiers", 0);
		message->AddInt64("when", system_time());
		message->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
		message->AddInt32("be:button", (int32)mask);
		message->AddInt32("be:device_id", fSerial);
		message->AddString("be:device_name", fDeviceRef.name);
		message->AddInt32(XV_FIELD_DEVICE_FLAGS,
			kXVDeviceHasPointer | kXVDeviceHasJoystick);
		fTarget.EnqueueMessage(message);
	}
}


void
GamepadDevice::_EmitKey(int32 haikuKey, int32 evdevCode, bool down)
{
	BMessage* message = new BMessage(down ? B_KEY_DOWN : B_KEY_UP);
	if (message == NULL)
		return;
	message->AddInt64("when", system_time());
	message->AddInt32("key", haikuKey);
	message->AddInt32("be:evdev_code", evdevCode);
	message->AddInt32("be:device_id", fSerial);
	message->AddString("be:device_name", fDeviceRef.name);
	message->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
	message->AddInt32("modifiers", 0);
	uint8 states[16];
	memset(states, 0, sizeof(states));
	message->AddData("states", B_UINT8_TYPE, states, sizeof(states));
	if (haikuKey == kKeyEnter)
		message->AddString("bytes", "\n");
	fTarget.EnqueueMessage(message);
}


void
GamepadDevice::_EmitWheel(float dx, float dy)
{
	BMessage* message = new BMessage(B_MOUSE_WHEEL_CHANGED);
	if (message == NULL)
		return;
	message->AddInt64("when", system_time());
	message->AddFloat("be:wheel_delta_x", dx);
	message->AddFloat("be:wheel_delta_y", dy);
	message->AddInt32("be:device_id", fSerial);
	message->AddString("be:device_name", fDeviceRef.name);
	message->AddInt32(XV_FIELD_DEVICE_FLAGS,
		kXVDeviceHasPointer | kXVDeviceHasJoystick);
	fTarget.EnqueueMessage(message);
}


void
GamepadDevice::HandleSeatMessage(uint32 /*what*/)
{
	// The add-on-level rescan covers recreated nodes.
}


void
GamepadDevice::UpdateScreenBounds(BRect frame, int32 /*orientation*/,
	int32 /*reflection*/)
{
	if (fTarget.fCursorLock.Lock()) {
		if (fTarget.fCursorPosition.x < 0)
			fTarget.fCursorPosition = BPoint(frame.Width() / 2.0f,
				frame.Height() / 2.0f);
		fTarget.fScreenFrame = frame;
		fTarget.fCursorLock.Unlock();
	}
}


//	#pragma mark - GamepadPointerDevice


GamepadPointerDevice::GamepadPointerDevice()
	:
	fDevices(2),
	fDeviceListLock("GamepadPointerDevice list"),
	fEnabledByDefault(false),
	fDeadzone(0.1f),
	fSensitivity(1.0f),
	fCursorPosition(-1, -1),
	fCursorLock("gamepad cursor lock"),
	fButtons(0),
	fOrientation(B_PANEL_ORIENTATION_NORMAL),
	fReflection(B_PANEL_REFLECTION_NONE)
{
	pointer_constraint_init(&fConstraint, 0, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f);
	_LoadSettings();

	StartMonitoringDevice(kDevicesDirectory);

	BEntry entry;
	BDirectory dir(kDevicesDirectory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (entry.IsDirectory()
			|| strncmp(path.Leaf(), "event", 5) != 0)
			continue;
		_AddDevice(path.Path());
	}
}


GamepadPointerDevice::~GamepadPointerDevice()
{
	StopMonitoringDevice(kDevicesDirectory);

	BObjectList<GamepadDevice, false> doomed;
	{
		BAutolock _(fDeviceListLock);
		for (int32 i = fDevices.CountItems() - 1; i >= 0; i--) {
			doomed.AddItem(fDevices.ItemAt(i));
			fDevices.RemoveItemAt(i);
		}
	}
	for (int32 i = 0; i < doomed.CountItems(); i++)
		delete doomed.ItemAt(i);
}


status_t
GamepadPointerDevice::InitCheck()
{
	return BInputServerDevice::InitCheck();
}


status_t
GamepadPointerDevice::Start(const char* /*name*/, void* cookie)
{
	GamepadDevice* device = (GamepadDevice*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	return device->Start();
}


status_t
GamepadPointerDevice::Stop(const char* /*name*/, void* cookie)
{
	GamepadDevice* device = (GamepadDevice*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	device->Stop();
	return B_OK;
}


status_t
GamepadPointerDevice::Control(const char* /*name*/, void* cookie,
	uint32 command, BMessage* message)
{
	GamepadDevice* device = (GamepadDevice*)cookie;

	if (command == B_NODE_MONITOR)
		return _HandleMonitor(message);

	if (command == B_SCREEN_BOUNDS_CHANGED) {
		BRect frame;
		int32 orientation = B_PANEL_ORIENTATION_NORMAL;
		int32 reflection = B_PANEL_REFLECTION_NONE;
		if (message == NULL
			|| message->FindRect("screen_bounds", &frame) != B_OK)
			return B_BAD_VALUE;
		message->FindInt32("screen_orientation", &orientation);
		message->FindInt32("screen_reflection", &reflection);

		{
			BAutolock _(fCursorLock);
			fScreenFrame = frame;
			fOrientation = orientation;
			fReflection = reflection;
		}
		if (device != NULL)
			device->UpdateScreenBounds(frame, orientation, reflection);
		return B_OK;
	}

	if (command == B_POINTER_CONSTRAINT_CHANGED)
		return _UpdatePointerConstraint(device, message);

	if (command == B_SEAT_DISABLED || command == B_SEAT_ENABLED
		|| command == B_SYSTEM_RESUMED) {
		if (cookie == NULL) {
			if (command == B_SYSTEM_RESUMED)
				_RescanDevices();
			return B_OK;
		}
		device->HandleSeatMessage(command);
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
GamepadPointerDevice::_HandleMonitor(BMessage* message)
{
	const char* path;
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK
		|| (opcode != B_ENTRY_CREATED && opcode != B_ENTRY_REMOVED)
		|| message->FindString("path", &path) != B_OK)
		return B_BAD_VALUE;

	if (opcode == B_ENTRY_CREATED)
		return _AddDevice(path);

	return _RemoveDevice(path);
}


void
GamepadPointerDevice::_RescanDevices()
{
	BEntry entry;
	BDirectory dir(kDevicesDirectory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (entry.IsDirectory()
			|| strncmp(path.Leaf(), "event", 5) != 0)
			continue;

		struct stat st;
		if (stat(path.Path(), &st) != 0 || !S_ISCHR(st.st_mode))
			continue;

		bool present = false;
		bool stale = false;
		{
			BAutolock _(fDeviceListLock);
			for (int32 i = 0; i < fDevices.CountItems(); i++) {
				GamepadDevice* device = fDevices.ItemAt(i);
				if (strcmp(device->Path(), path.Path()) != 0)
					continue;
				present = true;
				stale = device->Inode() != st.st_ino;
				break;
			}
		}

		if (present && !stale)
			continue;
		if (stale)
			_RemoveDevice(path.Path());
		_AddDevice(path.Path());
	}
}


GamepadDevice*
GamepadPointerDevice::_FindDevice(const char* path) const
{
	for (int32 i = fDevices.CountItems() - 1; i >= 0; i--) {
		GamepadDevice* device = fDevices.ItemAt(i);
		if (strcmp(device->Path(), path) == 0)
			return device;
	}
	return NULL;
}


status_t
GamepadPointerDevice::_AddDevice(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
		return B_BAD_TYPE;
	if (access(path, R_OK) != 0 && (errno == EACCES || errno == EPERM))
		return B_PERMISSION_DENIED;

	_RemoveDevice(path);

	BAutolock _(fDeviceListLock);
	if (_FindDevice(path) != NULL)
		return B_OK;

	GamepadDevice* device = new(std::nothrow) GamepadDevice(*this, path);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = device->Classify();
	if (status != B_OK) {
		delete device;
		return status;
	}

	if (!fDevices.AddItem(device)) {
		delete device;
		return B_NO_MEMORY;
	}

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;
	return RegisterDevices(devices);
}


GamepadDevice*
GamepadPointerDevice::_DetachDevice(const char* path, const int32* serial)
{
	GamepadDevice* device = NULL;
	{
		BAutolock _(fDeviceListLock);
		device = _FindDevice(path);
		if (device == NULL)
			return NULL;
		if (serial != NULL && device->Serial() != *serial)
			return NULL;
		fDevices.RemoveItem(device, false);
	}

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;
	UnregisterDevices(devices);
	return device;
}


status_t
GamepadPointerDevice::_RemoveDevice(const char* path)
{
	GamepadDevice* device = _DetachDevice(path, NULL);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;
	delete device;
	return B_OK;
}


status_t
GamepadPointerDevice::_RemoveDevice(const char* path, int32 serial)
{
	GamepadDevice* device = _DetachDevice(path, &serial);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;
	delete device;
	return B_OK;
}


void
GamepadPointerDevice::_LoadSettings()
{
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK
		|| settings.Append(kSettingsRelativePath) != B_OK)
		return;

	FILE* file = fopen(settings.Path(), "r");
	if (file == NULL)
		return;

	char line[128];
	while (fgets(line, sizeof(line), file) != NULL) {
		int enabled = fEnabledByDefault ? 1 : 0;
		if (sscanf(line, "enabled=%d", &enabled) == 1) {
			fEnabledByDefault = enabled != 0;
			continue;
		}
		float value;
		if (sscanf(line, "deadzone=%f", &value) == 1) {
			if (value >= 0.0f && value < 0.9f)
				fDeadzone = value;
			continue;
		}
		if (sscanf(line, "sensitivity=%f", &value) == 1) {
			if (value > 0.0f && value <= 10.0f)
				fSensitivity = value;
		}
	}
	fclose(file);
}


status_t
GamepadPointerDevice::_UpdatePointerConstraint(GamepadDevice* /*device*/,
	BMessage* message)
{
	int32 flags = 0;
	BRect rect;
	BPoint where;
	if (message == NULL || message->FindInt32("flags", &flags) != B_OK)
		return B_BAD_VALUE;
	message->FindRect("rect", &rect);
	message->FindPoint("where", &where);

	if (fConstraintLock.Lock()) {
		pointer_constraint_init(&fConstraint, flags, rect.left, rect.top,
			rect.right, rect.bottom, where.x, where.y);
		fConstraintLock.Unlock();
	}
	return B_OK;
}


void
GamepadPointerDevice::GetPointerConstraint(pointer_constraint* out) const
{
	if (out == NULL)
		return;

	if (fConstraintLock.Lock()) {
		*out = fConstraint;
		fConstraintLock.Unlock();
	} else
		pointer_constraint_init(out, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			0.0f);
}
