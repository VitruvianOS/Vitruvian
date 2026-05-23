/*
 * Copyright 2004-2025, Haiku.
 * Copyright 2026, The Vitruvian Project
 * Distributed under the terms of the GPL License.
 *
 * Authors:
 *		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 *		Jérôme Duval
 *		Axel Dörfler, axeld@pinc-software.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Stephan Aßmus, superstippi@gmx.de
 *		Samuel Rodríguez Pérez, samuelrp84@gmail.com
 *		Dario Casalinuovo
 */


#include "MouseInputDevice.h"

#include <algorithm>
#include <errno.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Screen.h>
#include <String.h>
#include <View.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include "LinuxEvdevShim.h"

#include <libinput.h>

#include <kb_mouse_settings.h>
#include <keyboard_mouse_driver.h>
#include <touchpad_settings.h>

#include "movement_maker.h"


#undef TRACE
//#define TRACE_MOUSE_DEVICE
#ifdef TRACE_MOUSE_DEVICE

	static int32 sFunctionDepth = -1;
#	define CALLED(x...)	do { \
		sFunctionDepth++; \
		debug_printf("%p -> %s {\n", this, __PRETTY_FUNCTION__); \
	} while (0)
#	define TRACE(x...)	do { BString _to; \
							_to.Append(' ', (sFunctionDepth + 1) * 2); \
							debug_printf("%p -> %s", this, _to.String()); \
							debug_printf(x); } while (0)
#	define LOG_EVENT(text...) do {} while (0)
#	define LOG_ERROR(text...) TRACE(text)
#else
#	define TRACE(x...) do {} while (0)
#	define CALLED(x...) TRACE(x)
#	define LOG_ERROR(x...) debug_printf(x)
#	define LOG_EVENT(x...) TRACE(x)
#endif


const static uint32 kMouseThreadPriority = B_FIRST_REAL_TIME_PRIORITY + 4;
const static char* kMouseDevicesDirectory = "/dev/input";


// ---------------------------------------------------------------------------
// libinput interface callbacks (path-mode, one context per gesture device)
// ---------------------------------------------------------------------------

static int
_libinput_open_restricted(const char* path, int flags, void* /*data*/)
{
	return open(path, flags);
}

static void
_libinput_close_restricted(int fd, void* /*data*/)
{
	close(fd);
}

static const struct libinput_interface kLibinputInterface = {
	.open_restricted  = _libinput_open_restricted,
	.close_restricted = _libinput_close_restricted,
};


// ---------------------------------------------------------------------------
// Device classification helper
// ---------------------------------------------------------------------------

static bool
_IsGestureDevice(int fd)
{
	// Only route direct-touch (touchscreen) devices to libinput.
	// INPUT_PROP_DIRECT means the device maps finger position directly to
	// screen coordinates (no pointer acceleration, no relative mode).
	//
	// Touchpads (INPUT_PROP_BUTTONPAD / INPUT_PROP_POINTER) and mice go
	// through the evdev path, which reads kernel-synthesised REL_WHEEL and
	// ABS coordinates directly — robust across QEMU lock state changes.
	uint8_t props[INPUT_PROP_CNT / 8 + 1] = {};
	if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) >= 0) {
		if (props[INPUT_PROP_DIRECT / 8] & (1 << (INPUT_PROP_DIRECT % 8)))
			return true;
	}
	return false;
}


// ---------------------------------------------------------------------------
// MouseDevice class declaration (full, with libinput fields)
// ---------------------------------------------------------------------------

class MouseDevice {
public:
								MouseDevice(MouseInputDevice& target,
									const char* path);
								~MouseDevice();

			status_t			Start();
			void				Stop();

			status_t			UpdateSettings();
			status_t			UpdateTouchpadSettings(const BMessage* message);

			const char*			Path() const { return fPath.String(); }
			input_device_ref*	DeviceRef() { return &fDeviceRef; }

private:
			char*				_BuildShortName() const;

	static	status_t			_ControlThreadEntry(void* arg);
			void				_ControlThread();
			void				_ControlThreadCleanup();
			void				_UpdateSettings();

			status_t			_GetTouchpadSettingsPath(BPath& path);
			status_t			_UpdateTouchpadSettings(BMessage* message);

			BMessage*			_BuildMouseMessage(uint32 what,
									uint64 when, uint32 buttons) const;
			void				_ComputeAcceleration(
									const mouse_movement& movements,
									int32& deltaX, int32& deltaY,
									float& historyDeltaX,
									float& historyDeltaY) const;
			uint32				_RemapButtons(uint32 buttons) const;

			// libinput event handling
			void				_LibinputHandleEvent(struct libinput_event* ev);

private:
			MouseInputDevice&	fTarget;
			BString				fPath;
			int					fDevice;
			struct libevdev*	fEvdevHandle;
			int					fEpollFd;

			// Libinput path (gesture devices)
			bool				fUseLibinput;
			struct libinput*	fLibinput;
			struct libinput_device* fLibinputDev;

			// Libinput per-device state
			uint32				fLibinputButtons;
			BPoint				fLibinputLastPos;
			float				fLibinputScrollAccX;
			float				fLibinputScrollAccY;
			bool				fLibinputTouchSlot1Active;
			float				fLibinputTouchSlot1LastX;
			float				fLibinputTouchSlot1LastY;
			float				fLibinputTouchScrollAccX;
			float				fLibinputTouchScrollAccY;
			bigtime_t			fLibinputLastClickTime;
			int32				fLibinputClickCount;
			uint32				fLibinputLastClickButton;
			// True when device is a direct-touch screen (TOUCH cap, no POINTER cap)
			// On touchscreens we synthesize B_MOUSE_DOWN/UP from TOUCH_DOWN/UP.
			bool				fLibinputIsDirectTouch;
			bool				fLibinputTouchSlot0Down;

			bool				fIsAbsolute;
			int32				fAbsMaxX, fAbsMaxY;
			int32				fLastAbsX, fLastAbsY;
			BPoint				fCursorPosition;
			int32				fScreenW, fScreenH;

			input_device_ref	fDeviceRef;
			mouse_settings		fSettings;
			bool				fDeviceRemapsButtons;

			thread_id			fThread;
	volatile bool				fActive;
	volatile bool				fUpdateSettings;

			bool				fIsTouchpad;
			TouchpadMovement	fTouchpadMovementMaker;
			BMessage*			fTouchpadSettingsMessage;
			BLocker				fTouchpadSettingsLock;
};


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) MouseInputDevice();
}


//	#pragma mark -


MouseDevice::MouseDevice(MouseInputDevice& target, const char* driverPath)
	:
	fTarget(target),
	fPath(driverPath),
	fDevice(-1),
	fEvdevHandle(NULL),
	fEpollFd(-1),
	fUseLibinput(false),
	fLibinput(NULL),
	fLibinputDev(NULL),
	fLibinputButtons(0),
	fLibinputLastPos(0, 0),
	fLibinputScrollAccX(0),
	fLibinputScrollAccY(0),
	fLibinputTouchSlot1Active(false),
	fLibinputTouchSlot1LastX(0),
	fLibinputTouchSlot1LastY(0),
	fLibinputTouchScrollAccX(0),
	fLibinputTouchScrollAccY(0),
	fLibinputLastClickTime(0),
	fLibinputClickCount(0),
	fLibinputLastClickButton(0),
	fLibinputIsDirectTouch(false),
	fLibinputTouchSlot0Down(false),
	fIsAbsolute(false),
	fAbsMaxX(65535),
	fAbsMaxY(65535),
	fLastAbsX(-1),
	fLastAbsY(-1),
	fCursorPosition(0, 0),
	fScreenW(1280),
	fScreenH(800),
	fDeviceRemapsButtons(false),
	fThread(-1),
	fActive(false),
	fUpdateSettings(false),
	fIsTouchpad(false),
	fTouchpadSettingsMessage(NULL),
	fTouchpadSettingsLock("Touchpad settings lock")
{
	CALLED();

	fDeviceRef.name = _BuildShortName();
	fDeviceRef.type = B_POINTING_DEVICE;
	fDeviceRef.cookie = this;

	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		fSettings.map.button[i] = B_MOUSE_BUTTON(i + 1);
	// Default speed so cursor moves before settings are loaded (speed=0 → frozen)
	fSettings.accel.speed = 65536;
}


MouseDevice::~MouseDevice()
{
	CALLED();
	TRACE("delete\n");

	if (fActive)
		Stop();

	free(fDeviceRef.name);
	delete fTouchpadSettingsMessage;
}


status_t
MouseDevice::Start()
{
	CALLED();

	// Open the device fd once to classify it
	int classifyFd = open(fPath.String(), O_RDONLY | O_NONBLOCK);
	if (classifyFd >= 0) {
		if (_IsGestureDevice(classifyFd)) {
			// Gesture device: hand off to libinput (path context)
			close(classifyFd);
			classifyFd = -1;

			fUseLibinput = true;

			fLibinput = libinput_path_create_context(&kLibinputInterface, NULL);
			if (fLibinput == NULL) {
				LOG_ERROR("MouseDevice: libinput_path_create_context failed for %s\n",
					fPath.String());
				fUseLibinput = false;
			} else {
				fLibinputDev = libinput_path_add_device(fLibinput,
					fPath.String());
				if (fLibinputDev == NULL) {
					LOG_ERROR("MouseDevice: libinput_path_add_device failed for %s\n",
						fPath.String());
					libinput_unref(fLibinput);
					fLibinput = NULL;
					fUseLibinput = false;
				} else {
					// libinput owns the device ref; add our own ref
					libinput_device_ref(fLibinputDev);

					// Initial dispatch so the DEVICE_ADDED event is consumed
					// (required before device config is accessible)
					libinput_dispatch(fLibinput);

					// Enable tap-to-click if the device supports it
					if (libinput_device_config_tap_get_finger_count(
							fLibinputDev) > 0) {
						libinput_device_config_tap_set_enabled(fLibinputDev,
							LIBINPUT_CONFIG_TAP_ENABLED);
					}

					// Detect direct-touch (touchscreen): has TOUCH cap, no POINTER cap.
					// On these devices we synthesize B_MOUSE_DOWN/UP from TOUCH events
					// rather than relying on POINTER_BUTTON (which doesn't fire for
					// raw touch screens).
					fLibinputIsDirectTouch =
						libinput_device_has_capability(fLibinputDev,
							LIBINPUT_DEVICE_CAP_TOUCH) &&
						!libinput_device_has_capability(fLibinputDev,
							LIBINPUT_DEVICE_CAP_POINTER);
				}
			}
		} else {
			// Check if it's a plain relative or absolute mouse via evdev
			struct libevdev* evdev = NULL;
			if (libevdev_new_from_fd(classifyFd, &evdev) < 0) {
				close(classifyFd);
				classifyFd = -1;
			} else {
				bool isRelMouse = libevdev_has_event_type(evdev, EV_REL)
					&& libevdev_has_event_code(evdev, EV_REL, REL_X)
					&& libevdev_has_event_code(evdev, EV_KEY, BTN_LEFT);
				bool isAbsMouse = !isRelMouse
					&& libevdev_has_event_type(evdev, EV_ABS)
					&& libevdev_has_event_code(evdev, EV_ABS, ABS_X)
					&& libevdev_has_event_code(evdev, EV_KEY, BTN_LEFT);

				if (!isRelMouse && !isAbsMouse) {
					libevdev_free(evdev);
					close(classifyFd);
					classifyFd = -1;
				} else {
					if (isAbsMouse) {
						fIsAbsolute = true;
						int mx = libevdev_get_abs_maximum(evdev, ABS_X);
						int my = libevdev_get_abs_maximum(evdev, ABS_Y);
						if (mx > 0) fAbsMaxX = mx;
						if (my > 0) fAbsMaxY = my;
					}
					fEvdevHandle = evdev;
					fDevice = classifyFd;
					classifyFd = -1;
				}
			}
		}
	}

	if (classifyFd >= 0) {
		close(classifyFd);
		classifyFd = -1;
	}

	char threadName[B_OS_NAME_LENGTH];
	snprintf(threadName, B_OS_NAME_LENGTH, "%s watcher", fDeviceRef.name);

	fThread = spawn_thread(_ControlThreadEntry, threadName,
		kMouseThreadPriority, (void*)this);

	status_t status;
	if (fThread < 0)
		status = fThread;
	else {
		fActive = true;
		status = resume_thread(fThread);
	}

	if (status < B_OK) {
		LOG_ERROR("%s: can't spawn/resume watching thread: %s\n",
			fDeviceRef.name, strerror(status));
		if (fDevice >= 0) {
			close(fDevice);
			fDevice = -1;
		}
		if (fLibinputDev != NULL) {
			libinput_device_unref(fLibinputDev);
			fLibinputDev = NULL;
		}
		if (fLibinput != NULL) {
			libinput_unref(fLibinput);
			fLibinput = NULL;
		}
		return status;
	}

	// Return B_OK if we successfully set up either evdev or libinput
	return (fDevice >= 0 || fUseLibinput) ? B_OK : B_ERROR;
}


void
MouseDevice::Stop()
{
	CALLED();

	fActive = false;

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

	// Clean up libinput resources
	if (fLibinputDev != NULL) {
		libinput_path_remove_device(fLibinputDev);
		libinput_device_unref(fLibinputDev);
		fLibinputDev = NULL;
	}
	if (fLibinput != NULL) {
		libinput_unref(fLibinput);
		fLibinput = NULL;
	}

	if (fThread >= 0) {
		suspend_thread(fThread);
		resume_thread(fThread);

		status_t dummy;
		wait_for_thread(fThread, &dummy);
	}
}


status_t
MouseDevice::UpdateSettings()
{
	CALLED();

	if (fThread < 0)
		return B_ERROR;

	fUpdateSettings = true;

	return B_OK;
}


status_t
MouseDevice::UpdateTouchpadSettings(const BMessage* message)
{
	if (!fIsTouchpad)
		return B_BAD_TYPE;
	if (fThread < 0)
		return B_ERROR;

	BAutolock _(fTouchpadSettingsLock);

	fUpdateSettings = true;

	delete fTouchpadSettingsMessage;
	fTouchpadSettingsMessage = new BMessage(*message);
	if (fTouchpadSettingsMessage == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


char*
MouseDevice::_BuildShortName() const
{
	BString string(fPath);
	BString deviceName;
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(deviceName, slash + 1, string.Length() - slash);
	// atoi("event3") = 0 because it stops at 'e'; skip non-digit prefix
	const char* p = deviceName.String();
	while (*p && (*p < '0' || *p > '9')) p++;
	int32 index = (*p ? atoi(p) : 0) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	if (name == "ps2")
		name = "PS/2";

	if (name.Length() <= 4)
		name.ToUpper();
	else
		name.Capitalize();

	if (string.FindFirst("touchpad") >= 0) {
		name << " Touchpad ";
	} else if (deviceName.FindFirst("trackpoint") >= 0) {
		name = "Trackpoint ";
	} else {
		if (deviceName.FindFirst("intelli") >= 0)
			name.Prepend("Extended ");

		name << " Mouse ";
	}
	name << index;

	return strdup(name.String());
}


// #pragma mark - control thread


status_t
MouseDevice::_ControlThreadEntry(void* arg)
{
	MouseDevice* device = (MouseDevice*)arg;
	device->_ControlThread();
	return B_OK;
}


void
MouseDevice::_ControlThread()
{
	CALLED();

	// Initialize screen size and sync cursor position
	{
		BScreen screen;
		BRect frame = screen.Frame();
		fScreenW = (int32)(frame.Width() + 1);
		fScreenH = (int32)(frame.Height() + 1);
	}
	if (fTarget.fCursorLock.Lock()) {
		if (fTarget.fCursorPosition.x < 0)
			fTarget.fCursorPosition.Set(fScreenW / 2.0f, fScreenH / 2.0f);
		fCursorPosition = fTarget.fCursorPosition;
		fLibinputLastPos = fTarget.fCursorPosition;
		fTarget.fCursorLock.Unlock();
	}

	if (fUseLibinput) {
		// ------------------------------------------------------------------
		// Libinput path: poll the libinput fd and dispatch events
		// ------------------------------------------------------------------
		debug_printf("MOUSE _ControlThread (libinput): path=%s\n",
			fPath.String());

		if (fLibinput == NULL) {
			_ControlThreadCleanup();
			return;
		}

		int lifd = libinput_get_fd(fLibinput);
		// Initial dispatch to consume DEVICE_ADDED events
		libinput_dispatch(fLibinput);

		while (fActive) {
			if (fUpdateSettings) {
				fUpdateSettings = false;
				_UpdateSettings();
			}

			struct pollfd pfd;
			pfd.fd = lifd;
			pfd.events = POLLIN;
			pfd.revents = 0;

			int ret = poll(&pfd, 1, 100);
			if (ret < 0) {
				if (errno == EINTR)
					continue;
				LOG_ERROR("Mouse libinput device exiting, poll error: %s\n",
					strerror(errno));
				_ControlThreadCleanup();
				return;
			}
			if (ret == 0)
				continue;

			libinput_dispatch(fLibinput);
			struct libinput_event* event;
			while ((event = libinput_get_event(fLibinput)) != NULL) {
				_LibinputHandleEvent(event);
				libinput_event_destroy(event);
			}
		}

	} else {
		// ------------------------------------------------------------------
		// Evdev path: existing epoll-based event loop
		// ------------------------------------------------------------------
		debug_printf("MOUSE _ControlThread: path=%s fDevice=%d\n",
			fPath.String(), fDevice);
		if (fDevice < 0) {
			_ControlThreadCleanup();
			return;
		}

		_UpdateSettings();
		debug_printf("MOUSE settings: speed=%d accel=%d\n",
			(int)fSettings.accel.speed, (int)fSettings.accel.accel_factor);

		uint32 lastButtons = 0;
		uint32 currentButtons = 0;

		fEpollFd = epoll_create1(0);
		if (fEpollFd < 0) {
			LOG_ERROR("Mouse: epoll_create1 failed: %s\n", strerror(errno));
			_ControlThreadCleanup();
			return;
		}

		struct epoll_event epev;
		epev.events = EPOLLIN;
		epev.data.fd = fDevice;
		if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fDevice, &epev) < 0) {
			LOG_ERROR("Mouse: epoll_ctl failed: %s\n", strerror(errno));
			_ControlThreadCleanup();
			return;
		}

		while (fActive) {
			if (fUpdateSettings) {
				fUpdateSettings = false;
				_UpdateSettings();
			}

			struct epoll_event fired;
			int n = epoll_wait(fEpollFd, &fired, 1, 100);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				LOG_ERROR("Mouse device exiting, epoll error: %s\n", strerror(errno));
				_ControlThreadCleanup();
				return;
			}
			if (n == 0)
				continue;

			// Accumulate events; flush on EV_SYN SYN_REPORT
			int32 xdelta = 0, ydelta = 0;
			int32 wheel_xdelta = 0, wheel_ydelta = 0;
			int32 currentAbsX = fLastAbsX, currentAbsY = fLastAbsY;
			bigtime_t timestamp = system_time();

			struct input_event iev;
			int rc;
			for (;;) {
				rc = libevdev_next_event(fEvdevHandle,
					LIBEVDEV_READ_FLAG_NORMAL, &iev);
				if (rc == -EAGAIN)
					break;
				if (rc == LIBEVDEV_READ_STATUS_SYNC) {
					while (libevdev_next_event(fEvdevHandle,
							LIBEVDEV_READ_FLAG_SYNC, &iev)
							== LIBEVDEV_READ_STATUS_SYNC)
						;
					continue;
				}
				if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
					break;

				timestamp = (bigtime_t)iev.time.tv_sec * 1000000LL
					+ iev.time.tv_usec;

				if (iev.type == EV_REL) {
					switch (iev.code) {
						case REL_X:      xdelta += iev.value;         break;
						case REL_Y:      ydelta += iev.value;         break;
						// Linux REL_WHEEL positive = scroll up;
						// Haiku be:wheel_delta_y positive = scroll down
						case REL_WHEEL:  wheel_ydelta += -iev.value;  break;
						case REL_HWHEEL: wheel_xdelta += iev.value;   break;
					}
				} else if (iev.type == EV_ABS) {
					switch (iev.code) {
						case ABS_X: currentAbsX = iev.value; break;
						case ABS_Y: currentAbsY = iev.value; break;
					}
				} else if (iev.type == EV_KEY) {
					uint32 bit = 0;
					switch (iev.code) {
						case BTN_LEFT:   bit = 0x01; break;
						case BTN_RIGHT:  bit = 0x02; break;
						case BTN_MIDDLE: bit = 0x04; break;
						case BTN_SIDE:   bit = 0x08; break;
						case BTN_EXTRA:  bit = 0x10; break;
					}
					if (bit != 0) {
						if (iev.value)
							currentButtons |= bit;
						else
							currentButtons &= ~bit;
					}
				} else if (iev.type == EV_SYN && iev.code == SYN_REPORT) {
					break;
				}
			}

			// Update cursor position.  For relative devices, read the latest shared
			// position first so deltas from this device compose with moves from any
			// other device (e.g. a touchpad that was used moments ago).
			if (fIsAbsolute && currentAbsX >= 0) {
				fCursorPosition.x = (float)currentAbsX / fAbsMaxX * fScreenW;
				fCursorPosition.y = (float)currentAbsY / fAbsMaxY * fScreenH;
				fLastAbsX = currentAbsX;
				fLastAbsY = currentAbsY;
				xdelta = 1; // non-zero to trigger movement message
				ydelta = 0;
			} else if (!fIsAbsolute && (xdelta != 0 || ydelta != 0)) {
				// Sync from shared before applying delta.
				if (fTarget.fCursorLock.Lock()) {
					fCursorPosition = fTarget.fCursorPosition;
					fTarget.fCursorLock.Unlock();
				}
				float histX = 0, histY = 0;
				mouse_movement mv;
				memset(&mv, 0, sizeof(mv));
				mv.xdelta = xdelta; mv.ydelta = ydelta;
				mv.timestamp = timestamp;
				int32 dX, dY;
				_ComputeAcceleration(mv, dX, dY, histX, histY);
				fCursorPosition.x += dX;
				fCursorPosition.y += dY;
				fCursorPosition.x = std::max(0.0f,
					std::min((float)(fScreenW - 1), fCursorPosition.x));
				fCursorPosition.y = std::max(0.0f,
					std::min((float)(fScreenH - 1), fCursorPosition.y));
			}
			// Write back to shared slot so the next device starts from here.
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fCursorPosition = fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			bool hasMoved = (fIsAbsolute && fLastAbsX >= 0)
				|| (!fIsAbsolute && (xdelta != 0 || ydelta != 0));
			uint32 changedButtons = lastButtons ^ currentButtons;

			if (!hasMoved && changedButtons == 0
					&& wheel_xdelta == 0 && wheel_ydelta == 0)
				continue;

			uint32 remappedButtons = _RemapButtons(currentButtons);

			// Button events
			if (changedButtons != 0) {
				bool pressed = (changedButtons & currentButtons) != 0;
				BMessage* message = _BuildMouseMessage(
					pressed ? B_MOUSE_DOWN : B_MOUSE_UP,
					timestamp, remappedButtons);
				if (message != NULL) {
					if (pressed)
						message->AddInt32("clicks", 1);
					fTarget.EnqueueMessage(message);
					lastButtons = currentButtons;
				}
			}

			// Movement
			if (hasMoved) {
				BMessage* message = _BuildMouseMessage(B_MOUSE_MOVED,
					timestamp, remappedButtons);
				if (message != NULL) {
					fTarget.EnqueueMessage(message);
				}
			}

			// Scroll wheel
			if (wheel_ydelta != 0 || wheel_xdelta != 0) {
				BMessage* message = new BMessage(B_MOUSE_WHEEL_CHANGED);
				if (message != NULL) {
					message->AddInt64("when", timestamp);
					message->AddFloat("be:wheel_delta_x", wheel_xdelta);
					message->AddFloat("be:wheel_delta_y", wheel_ydelta);
					fTarget.EnqueueMessage(message);
				}
			}
		}
	}
}


void
MouseDevice::_ControlThreadCleanup()
{
	if (fActive) {
		fThread = -1;
		fTarget._RemoveDevice(fPath.String());
	}
}


void
MouseDevice::_UpdateSettings()
{
	CALLED();

	if (get_mouse_map(fDeviceRef.name, &fSettings.map) != B_OK)
		LOG_ERROR("error when get_mouse_map\n");
	else
		fDeviceRemapsButtons = ioctl(fDevice, MS_SET_MAP, &fSettings.map) == B_OK;

	if (get_click_speed(fDeviceRef.name, &fSettings.click_speed) == B_OK) {
		if (fIsTouchpad)
			fTouchpadMovementMaker.click_speed = fSettings.click_speed;
		ioctl(fDevice, MS_SET_CLICKSPEED, &fSettings.click_speed);
	} else
		LOG_ERROR("error when get_click_speed\n");

	if (get_mouse_speed(fDeviceRef.name, &fSettings.accel.speed) != B_OK)
		LOG_ERROR("error when get_mouse_speed\n");
	else {
		if (get_mouse_acceleration(fDeviceRef.name,
				&fSettings.accel.accel_factor) != B_OK)
			LOG_ERROR("error when get_mouse_acceleration\n");
		else {
			mouse_accel accel;
			ioctl(fDevice, MS_GET_ACCEL, &accel);
			accel.speed = fSettings.accel.speed;
			accel.accel_factor = fSettings.accel.accel_factor;
			ioctl(fDevice, MS_SET_ACCEL, &fSettings.accel);
		}
	}

	if (get_mouse_type(fDeviceRef.name, &fSettings.type) != B_OK)
		LOG_ERROR("error when get_mouse_type\n");
	else
		ioctl(fDevice, MS_SET_TYPE, &fSettings.type);
}


status_t
MouseDevice::_GetTouchpadSettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;
	return path.Append(TOUCHPAD_SETTINGS_FILE);
}


status_t
MouseDevice::_UpdateTouchpadSettings(BMessage* message)
{
	touchpad_settings settings;
	settings = kDefaultTouchpadSettings;

	message->FindBool("scroll_reverse", &settings.scroll_reverse);
	message->FindBool("scroll_twofinger", &settings.scroll_twofinger);
	message->FindBool("scroll_twofinger_horizontal",
		&settings.scroll_twofinger_horizontal);
	message->FindFloat("scroll_rightrange", &settings.scroll_rightrange);
	message->FindFloat("scroll_bottomrange", &settings.scroll_bottomrange);

	message->FindInt16("scroll_xstepsize", (int16*)&settings.scroll_xstepsize);
	message->FindInt16("scroll_ystepsize", (int16*)&settings.scroll_ystepsize);
	message->FindInt8("scroll_acceleration",
		(int8*)&settings.scroll_acceleration);
	message->FindInt8("tapgesture_sensibility",
		(int8*)&settings.tapgesture_sensibility);

	message->FindBool("scroll_twofinger_natural_scrolling",
		&settings.scroll_twofinger_natural_scrolling);
	message->FindInt8("edge_motion", (int8*)&settings.edge_motion);
	message->FindBool("finger_click", &settings.finger_click);
	message->FindBool("software_button_areas", &settings.software_button_areas);

	if (fIsTouchpad)
		fTouchpadMovementMaker.SetSettings(settings);

	return B_OK;
}


BMessage*
MouseDevice::_BuildMouseMessage(uint32 what, uint64 when,
	uint32 buttons) const
{
	BMessage* message = new BMessage(what);
	if (message == NULL)
		return NULL;

	if (message->AddPoint("where", fCursorPosition) < B_OK
		|| message->AddInt32("buttons", buttons) < B_OK
		|| message->AddInt32("modifiers", 0) < B_OK
		|| message->AddInt64("when", when) < B_OK
		|| message->AddInt32("be:device_subtype",
			fIsTouchpad ? B_TOUCHPAD_POINTING_DEVICE
				: B_MOUSE_POINTING_DEVICE) < B_OK) {
		delete message;
		return NULL;
	}

	return message;
}


void
MouseDevice::_ComputeAcceleration(const mouse_movement& movements,
	int32& _deltaX, int32& _deltaY, float& historyDeltaX,
	float& historyDeltaY) const
{
	float deltaX = (float)movements.xdelta * fSettings.accel.speed / 65536.0
		+ historyDeltaX;
	float deltaY = (float)movements.ydelta * fSettings.accel.speed / 65536.0
		+ historyDeltaY;

	double acceleration = 1;
	if (fSettings.accel.accel_factor) {
		acceleration = 1 + sqrt(deltaX * deltaX + deltaY * deltaY)
			* fSettings.accel.accel_factor / 524288.0;
	}

	deltaX *= acceleration;
	deltaY *= acceleration;

	if (deltaX >= 0)
		_deltaX = (int32)floorf(deltaX);
	else
		_deltaX = (int32)ceilf(deltaX);

	if (deltaY >= 0)
		_deltaY = (int32)floorf(deltaY);
	else
		_deltaY = (int32)ceilf(deltaY);

	historyDeltaX = deltaX - _deltaX;
	historyDeltaY = deltaY - _deltaY;
}


uint32
MouseDevice::_RemapButtons(uint32 buttons) const
{
	if (fDeviceRemapsButtons)
		return buttons;

	uint32 newButtons = 0;
	for (int32 i = 0; buttons; i++) {
		if (buttons & 0x1)
			newButtons |= fSettings.map.button[i];
		buttons >>= 1;
	}

	return newButtons;
}


// #pragma mark - libinput event handling


void
MouseDevice::_LibinputHandleEvent(struct libinput_event* event)
{
	enum libinput_event_type type = libinput_event_get_type(event);

	switch (type) {
		case LIBINPUT_EVENT_POINTER_MOTION:
		{
			// Relative motion from touchpad pointer mode
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			double dx = libinput_event_pointer_get_dx(pev);
			double dy = libinput_event_pointer_get_dy(pev);

			// Sync from shared slot before applying delta
			if (fTarget.fCursorLock.Lock()) {
				fLibinputLastPos = fTarget.fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			fLibinputLastPos.x += (float)dx;
			fLibinputLastPos.y += (float)dy;
			fLibinputLastPos.x = std::max(0.0f,
				std::min((float)(fScreenW - 1), fLibinputLastPos.x));
			fLibinputLastPos.y = std::max(0.0f,
				std::min((float)(fScreenH - 1), fLibinputLastPos.y));

			// Write back to shared slot
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fCursorPosition = fLibinputLastPos;
				fTarget.fCursorLock.Unlock();
			}

			BMessage* msg = new BMessage(B_MOUSE_MOVED);
			msg->AddPoint("where", fLibinputLastPos);
			msg->AddInt32("buttons", (int32)fLibinputButtons);
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_TOUCHPAD_POINTING_DEVICE);
			fTarget.EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		{
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			// Use current screen size (read once at thread start)
			double x = libinput_event_pointer_get_absolute_x_transformed(
				pev, fScreenW);
			double y = libinput_event_pointer_get_absolute_y_transformed(
				pev, fScreenH);

			fLibinputLastPos.x = (float)x;
			fLibinputLastPos.y = (float)y;

			// Sync shared cursor position
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fCursorPosition = fLibinputLastPos;
				fTarget.fCursorLock.Unlock();
			}

			BMessage* msg = new BMessage(B_MOUSE_MOVED);
			msg->AddPoint("where", fLibinputLastPos);
			msg->AddInt32("buttons", (int32)fLibinputButtons);
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
			fTarget.EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_BUTTON:
		{
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);
			uint32_t button = libinput_event_pointer_get_button(pev);
			enum libinput_button_state state =
				libinput_event_pointer_get_button_state(pev);

			uint32 beButton = 0;
			if (button == BTN_LEFT)         beButton = 0x1;
			else if (button == BTN_RIGHT)   beButton = 0x2;
			else if (button == BTN_MIDDLE)  beButton = 0x4;

			if (state == LIBINPUT_BUTTON_STATE_PRESSED)
				fLibinputButtons |= beButton;
			else
				fLibinputButtons &= ~beButton;

			// Read current cursor pos from shared state
			BPoint where = fLibinputLastPos;
			if (fTarget.fCursorLock.Lock()) {
				where = fTarget.fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			uint32 what = (state == LIBINPUT_BUTTON_STATE_PRESSED)
				? B_MOUSE_DOWN : B_MOUSE_UP;

			BMessage* msg = new BMessage(what);
			msg->AddPoint("where", where);
			msg->AddInt32("buttons", (int32)fLibinputButtons);
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_TOUCHPAD_POINTING_DEVICE);
			if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
				bigtime_t now = system_time();
				if (beButton == fLibinputLastClickButton
					&& (now - fLibinputLastClickTime) < 300000LL)
					fLibinputClickCount++;
				else
					fLibinputClickCount = 1;
				fLibinputLastClickTime = now;
				fLibinputLastClickButton = beButton;
				msg->AddInt32("clicks", fLibinputClickCount);
				msg->AddInt32("be:button", (int32)beButton);
			}
			fTarget.EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
		{
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			double rawDx = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
			double rawDy = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

			fLibinputScrollAccX += rawDx;
			fLibinputScrollAccY += rawDy;

			const float kStepSize = 1.0f;
			int32 stepsX = 0, stepsY = 0;

			if (fLibinputScrollAccX >= kStepSize
				|| fLibinputScrollAccX <= -kStepSize) {
				stepsX = (int32)(fLibinputScrollAccX / kStepSize);
				fLibinputScrollAccX -= stepsX * kStepSize;
			}
			if (fLibinputScrollAccY >= kStepSize
				|| fLibinputScrollAccY <= -kStepSize) {
				stepsY = (int32)(fLibinputScrollAccY / kStepSize);
				fLibinputScrollAccY -= stepsY * kStepSize;
			}

			if (stepsX == 0 && stepsY == 0)
				break;

			BPoint where = fLibinputLastPos;
			if (fTarget.fCursorLock.Lock()) {
				where = fTarget.fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
			msg->AddInt64("when", system_time());
			msg->AddFloat("be:wheel_delta_x", (float)stepsX);
			msg->AddFloat("be:wheel_delta_y", (float)stepsY);
			msg->AddPoint("where", where);
			msg->AddInt32("buttons", 0);
			msg->AddInt32("modifiers", 0);
			fTarget.EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
		{
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			double dx = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
			double dy = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

			fLibinputScrollAccX += dx;
			fLibinputScrollAccY += dy;

			const float kStepSize = 1.0f;
			int32 stepsX = 0, stepsY = 0;

			if (fLibinputScrollAccX >= kStepSize
				|| fLibinputScrollAccX <= -kStepSize) {
				stepsX = (int32)(fLibinputScrollAccX / kStepSize);
				fLibinputScrollAccX -= stepsX * kStepSize;
			}
			if (fLibinputScrollAccY >= kStepSize
				|| fLibinputScrollAccY <= -kStepSize) {
				stepsY = (int32)(fLibinputScrollAccY / kStepSize);
				fLibinputScrollAccY -= stepsY * kStepSize;
			}

			if (stepsX == 0 && stepsY == 0)
				break;

			BPoint where = fLibinputLastPos;
			if (fTarget.fCursorLock.Lock()) {
				where = fTarget.fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
			msg->AddInt64("when", system_time());
			msg->AddFloat("be:wheel_delta_x", (float)stepsX);
			msg->AddFloat("be:wheel_delta_y", (float)-stepsY);
			msg->AddPoint("where", where);
			msg->AddInt32("buttons", 0);
			msg->AddInt32("modifiers", 0);
			fTarget.EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_TOUCH_DOWN:
		case LIBINPUT_EVENT_TOUCH_UP:
		case LIBINPUT_EVENT_TOUCH_MOTION:
		{
			struct libinput_event_touch* tev =
				libinput_event_get_touch_event(event);
			int32_t slot = libinput_event_touch_get_slot(tev);

			// Slot 1 = second finger: track for two-finger scroll
			if (slot == 1) {
				if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
					float sx = (float)libinput_event_touch_get_x_transformed(
						tev, fScreenW);
					float sy = (float)libinput_event_touch_get_y_transformed(
						tev, fScreenH);
					fLibinputTouchSlot1Active = true;
					fLibinputTouchSlot1LastX  = sx;
					fLibinputTouchSlot1LastY  = sy;
					fLibinputTouchScrollAccX  = 0;
					fLibinputTouchScrollAccY  = 0;
				} else if (type == LIBINPUT_EVENT_TOUCH_UP) {
					fLibinputTouchSlot1Active = false;
					fLibinputTouchScrollAccX  = 0;
					fLibinputTouchScrollAccY  = 0;
				} else if (type == LIBINPUT_EVENT_TOUCH_MOTION
					&& fLibinputTouchSlot1Active) {
					float sx = (float)libinput_event_touch_get_x_transformed(
						tev, fScreenW);
					float sy = (float)libinput_event_touch_get_y_transformed(
						tev, fScreenH);
					float dx = sx - fLibinputTouchSlot1LastX;
					float dy = sy - fLibinputTouchSlot1LastY;
					fLibinputTouchSlot1LastX = sx;
					fLibinputTouchSlot1LastY = sy;

					fLibinputTouchScrollAccX += dx;
					fLibinputTouchScrollAccY += dy;

					const float kScrollStep = 30.0f;
					int32 stepsX = 0, stepsY = 0;
					if (fLibinputTouchScrollAccX >= kScrollStep
						|| fLibinputTouchScrollAccX <= -kScrollStep) {
						stepsX = (int32)(fLibinputTouchScrollAccX / kScrollStep);
						fLibinputTouchScrollAccX -= stepsX * kScrollStep;
					}
					if (fLibinputTouchScrollAccY >= kScrollStep
						|| fLibinputTouchScrollAccY <= -kScrollStep) {
						stepsY = (int32)(fLibinputTouchScrollAccY / kScrollStep);
						fLibinputTouchScrollAccY -= stepsY * kScrollStep;
					}

					if (stepsX != 0 || stepsY != 0) {
						BPoint where = fLibinputLastPos;
						if (fTarget.fCursorLock.Lock()) {
							where = fTarget.fCursorPosition;
							fTarget.fCursorLock.Unlock();
						}

						BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
						msg->AddInt64("when", system_time());
						msg->AddFloat("be:wheel_delta_x", (float)stepsX);
						msg->AddFloat("be:wheel_delta_y", (float)stepsY);
						msg->AddPoint("where", where);
						msg->AddInt32("buttons", 0);
						msg->AddInt32("modifiers", 0);
						fTarget.EnqueueMessage(msg);
					}
				}
				break;
			}

			// Slot 0 = first finger
			if (slot != 0)
				break;

			if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
				float sx = (float)libinput_event_touch_get_x_transformed(
					tev, fScreenW);
				float sy = (float)libinput_event_touch_get_y_transformed(
					tev, fScreenH);
				fLibinputLastPos.x = sx;
				fLibinputLastPos.y = sy;

				if (fTarget.fCursorLock.Lock()) {
					fTarget.fCursorPosition = fLibinputLastPos;
					fTarget.fCursorLock.Unlock();
				}

				// Touchscreen: synthesize B_MOUSE_DOWN.
				// Touchpad: B_MOUSE_DOWN comes from POINTER_BUTTON (tap-to-click).
				if (fLibinputIsDirectTouch) {
					fLibinputTouchSlot0Down = true;
					BMessage* msg = new BMessage(B_MOUSE_DOWN);
					msg->AddPoint("where", fLibinputLastPos);
					msg->AddInt32("buttons", 1);
					msg->AddInt32("modifiers", 0);
					msg->AddInt64("when", system_time());
					msg->AddInt32("clicks", 1);
					msg->AddInt32("be:button", 1);
					msg->AddInt32("be:device_subtype",
						B_TOUCHPAD_POINTING_DEVICE);
					fTarget.EnqueueMessage(msg);
				}
			} else if (type == LIBINPUT_EVENT_TOUCH_UP) {
				if (fLibinputIsDirectTouch && fLibinputTouchSlot0Down) {
					fLibinputTouchSlot0Down = false;
					BMessage* msg = new BMessage(B_MOUSE_UP);
					msg->AddPoint("where", fLibinputLastPos);
					msg->AddInt32("buttons", 0);
					msg->AddInt32("modifiers", 0);
					msg->AddInt64("when", system_time());
					msg->AddInt32("be:device_subtype",
						B_TOUCHPAD_POINTING_DEVICE);
					fTarget.EnqueueMessage(msg);
				}
			} else if (type == LIBINPUT_EVENT_TOUCH_MOTION) {
				// Skip pointer move while two-finger scroll is active
				if (fLibinputTouchSlot1Active)
					break;

				float sx = (float)libinput_event_touch_get_x_transformed(
					tev, fScreenW);
				float sy = (float)libinput_event_touch_get_y_transformed(
					tev, fScreenH);
				fLibinputLastPos.x = sx;
				fLibinputLastPos.y = sy;

				if (fTarget.fCursorLock.Lock()) {
					fTarget.fCursorPosition = fLibinputLastPos;
					fTarget.fCursorLock.Unlock();
				}

				// For touchscreen: emit B_MOUSE_MOVED with button held (drag).
				// For touchpad: only emit when no pointer button is held.
				uint32 moveBtns = 0;
				bool shouldEmit = false;
				if (fLibinputIsDirectTouch) {
					moveBtns = fLibinputTouchSlot0Down ? 1 : 0;
					shouldEmit = true;
				} else if (fLibinputButtons == 0) {
					moveBtns = 0;
					shouldEmit = true;
				}

				if (shouldEmit) {
					BMessage* msg = new BMessage(B_MOUSE_MOVED);
					msg->AddPoint("where", fLibinputLastPos);
					msg->AddInt32("buttons", (int32)moveBtns);
					msg->AddInt32("modifiers", 0);
					msg->AddInt64("when", system_time());
					msg->AddInt32("be:device_subtype",
						B_TOUCHPAD_POINTING_DEVICE);
					fTarget.EnqueueMessage(msg);
				}
			}
			break;
		}

		default:
			break;
	}
}


//	#pragma mark -


MouseInputDevice::MouseInputDevice()
	:
	fDevices(2),
	fDeviceListLock("MouseInputDevice list"),
	fCursorPosition(-1, -1),
	fCursorLock("cursor position lock")
{
	CALLED();

	StartMonitoringDevice(kMouseDevicesDirectory);
	_RecursiveScan(kMouseDevicesDirectory);
}


MouseInputDevice::~MouseInputDevice()
{
	CALLED();

	StopMonitoringDevice(kMouseDevicesDirectory);
	fDevices.MakeEmpty();
}


status_t
MouseInputDevice::InitCheck()
{
	CALLED();

	return BInputServerDevice::InitCheck();
}


status_t
MouseInputDevice::Start(const char* name, void* cookie)
{
	CALLED();

	MouseDevice* device = (MouseDevice*)cookie;

	return device->Start();
}


status_t
MouseInputDevice::Stop(const char* name, void* cookie)
{
	TRACE("%s(%s)\n", __PRETTY_FUNCTION__, name);

	MouseDevice* device = (MouseDevice*)cookie;
	device->Stop();

	return B_OK;
}


status_t
MouseInputDevice::Control(const char* name, void* cookie,
	uint32 command, BMessage* message)
{
	TRACE("%s(%s, code: %" B_PRIu32 ")\n", __PRETTY_FUNCTION__, name, command);

	MouseDevice* device = (MouseDevice*)cookie;

	if (command == B_NODE_MONITOR)
		return _HandleMonitor(message);

	if (command == B_SET_TOUCHPAD_SETTINGS)
		return device->UpdateTouchpadSettings(message);

	if (command >= B_MOUSE_TYPE_CHANGED
		&& command <= B_MOUSE_ACCELERATION_CHANGED)
		return device->UpdateSettings();

	return B_BAD_VALUE;
}


status_t
MouseInputDevice::_HandleMonitor(BMessage* message)
{
	CALLED();

	const char* path;
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK
		|| (opcode != B_ENTRY_CREATED && opcode != B_ENTRY_REMOVED)
		|| message->FindString("path", &path) != B_OK)
		return B_BAD_VALUE;

	if (opcode == B_ENTRY_CREATED)
		return _AddDevice(path);

#if 0
	return _RemoveDevice(path);
#else
	// Don't handle B_ENTRY_REMOVED, let the control thread take care of it.
	return B_OK;
#endif
}


void
MouseInputDevice::_RecursiveScan(const char* directory)
{
	CALLED();

	BEntry entry;
	BDirectory dir(directory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);

		// Only open evdev event nodes; skip subdirs (by-id, by-path, etc.)
		if (entry.IsDirectory())
			continue;
		if (strncmp(path.Leaf(), "event", 5) == 0)
			_AddDevice(path.Path());
	}
}


MouseDevice*
MouseInputDevice::_FindDevice(const char* path) const
{
	CALLED();

	for (int32 i = fDevices.CountItems() - 1; i >= 0; i--) {
		MouseDevice* device = fDevices.ItemAt(i);
		if (strcmp(device->Path(), path) == 0)
			return device;
	}

	return NULL;
}


status_t
MouseInputDevice::_AddDevice(const char* path)
{
	CALLED();

	BAutolock _(fDeviceListLock);

	_RemoveDevice(path);

	MouseDevice* device = new(std::nothrow) MouseDevice(*this, path);
	if (device == NULL) {
		TRACE("No memory\n");
		return B_NO_MEMORY;
	}

	if (!fDevices.AddItem(device)) {
		TRACE("No memory in list\n");
		delete device;
		return B_NO_MEMORY;
	}

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	TRACE("adding path: %s, name: %s\n", path, devices[0]->name);

	return RegisterDevices(devices);
}


status_t
MouseInputDevice::_RemoveDevice(const char* path)
{
	CALLED();

	BAutolock _(fDeviceListLock);
	MouseDevice* device = _FindDevice(path);
	if (device == NULL) {
		TRACE("%s not found\n", path);
		return B_ENTRY_NOT_FOUND;
	}

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	TRACE("removing path: %s, name: %s\n", path, devices[0]->name);

	UnregisterDevices(devices);

	fDevices.RemoveItem(device);

	return B_OK;
}
