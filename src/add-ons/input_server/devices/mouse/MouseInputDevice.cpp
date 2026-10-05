/*
 * Copyright 2004-2025, Haiku.
 * Copyright 2026, The Vitruvian Project
 * Copyright 2026, Dario Casalinuovo.
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

#include "UdevDeviceName.h"

#include <algorithm>
#include <errno.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AppDefs.h>
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
#include <sys/stat.h>
#include "LinuxEvdevShim.h"
#include "PanelOrientationTransform.h"

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

			// Called once from _AddDevice(); Start() must not re-probe.
			status_t			_Classify();

			status_t			UpdateSettings();
			status_t			UpdateTouchpadSettings(const BMessage* message);
			void				HandleSeatMessage(uint32 what);

			void				UpdateScreenBounds(BRect frame,
									int32 orientation, int32 reflection);

			// Hardware name, distinct from fDeviceRef.name (the
			// identity/settings key).
			status_t			GetDescription(BMessage* message) const;

			const char*			Path() const { return fPath.String(); }
			input_device_ref*	DeviceRef() { return &fDeviceRef; }

			// Identity for removal; immune to ABA (see fSerial).
			int32				Serial() const { return fSerial; }
			ino_t				Inode() const { return fDeviceInode; }

private:
			char*				_BuildShortName() const;

	static	status_t			_ControlThreadEntry(void* arg);
			void				_ControlThread();
			void				_ControlThreadCleanup();
			void				_UpdateSettings();

			status_t			_GetTouchpadSettingsPath(BPath& path);
			status_t			_UpdateTouchpadSettings(BMessage* message);
			void				_UpdateTouchpadScale();

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
			// Normalizes, rotates by fOrientation and scales to screen.
			void				_RotateDelta(float dx, float dy,
									float& outDx, float& outDy) const
								{
									rotate_panel_delta(fOrientation,
										dx, dy, outDx, outDy, fReflection);
								}
			// Normalizes and rotates by fOrientation.
			void				_RotatePoint(float u, float v,
									float& outU, float& outV) const
								{
									rotate_panel_point(fOrientation,
										u, v, outU, outV, fReflection);
								}

			BPoint				_TouchPoint(
									struct libinput_event_touch* tev) const;

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

			bigtime_t			fLastClickTime;
			int32				fClickCount;
			uint32				fLastClickButtons;

			bool				fIsAbsolute;
			// A clickpad reports finger position on the pad, so it
			// needs delta tracking, not a position jump.
			bool				fIsAbsoluteTouchpad;
			// Screen pixels per pad unit; 0 until screen bounds are known.
			float				fTouchpadScaleX;
			float				fTouchpadScaleY;
			// Sub-pixel remainder so small pad moves are not dropped.
			float				fTouchpadResidualX;
			float				fTouchpadResidualY;
			uint32				fSharedButtons;
			int32				fAbsMinX, fAbsMinY;
			// Pad units per mm; 0 when the driver does not say.
			int32				fAbsResX, fAbsResY;
			int32				fAbsMaxX, fAbsMaxY;
			int32				fLastAbsX, fLastAbsY;
			BPoint				fCursorPosition;
			int32				fScreenW, fScreenH;
			// DRM_MODE_PANEL_ORIENTATION_*, see PanelOrientationTransform.h
			int32				fOrientation;
			// B_PANEL_REFLECTION_*, see PanelOrientationTransform.h
			int32				fReflection;

			input_device_ref	fDeviceRef;
			mouse_settings		fSettings;
			bool				fDeviceRemapsButtons;

			int32				fSerial;
			// st_ino of fDevice at classify time; resume re-probe can
			// recreate the node at the same path with a new inode.
			ino_t				fDeviceInode;

			thread_id			fThread;
	volatile bool				fActive;
			// Claimed with atomic_get_and_set(), so int32 and not
			// volatile; matches the keyboard add-on.
			int32				fUpdateSettings;

			// Atomic pending B_SEAT_DISABLED / B_SEAT_ENABLED /
			// B_SYSTEM_RESUMED, 0 = none.
			int32				fSeatCommand;

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


// Monotonic; a heap address can be handed out twice (ABA).
static int32 sNextMouseDeviceSerial = 0;


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
	fLastClickTime(0),
	fClickCount(0),
	fLastClickButtons(0),
	fIsAbsolute(false),
	fIsAbsoluteTouchpad(false),
	fTouchpadScaleX(0.0f),
	fTouchpadScaleY(0.0f),
	fTouchpadResidualX(0.0f),
	fTouchpadResidualY(0.0f),
	fSharedButtons(0),
	fAbsMinX(0),
	fAbsMinY(0),
	fAbsResX(0),
	fAbsResY(0),
	fAbsMaxX(65535),
	fAbsMaxY(65535),
	fLastAbsX(-1),
	fLastAbsY(-1),
	fCursorPosition(0, 0),
	fScreenW(1280),
	fScreenH(800),
	fOrientation(B_PANEL_ORIENTATION_NORMAL),
	fReflection(B_PANEL_REFLECTION_NONE),
	fDeviceRemapsButtons(false),
	fSerial(atomic_add(&sNextMouseDeviceSerial, 1)),
	fDeviceInode(0),
	fThread(-1),
	fActive(false),
	fUpdateSettings(0),
	fSeatCommand(0),
	fIsTouchpad(false),
	fTouchpadSettingsMessage(NULL),
	fTouchpadSettingsLock("Touchpad settings lock")
{
	CALLED();

	fDeviceRef.name = _BuildShortName();
	fDeviceRef.type = B_POINTING_DEVICE;
	fDeviceRef.cookie = this;

	memset(&fSettings, 0, sizeof(fSettings));

	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		fSettings.map.button[i] = B_MOUSE_BUTTON(i + 1);
	// Default speed so cursor moves before settings are loaded (speed=0 -> frozen)
	fSettings.accel.speed = 65536;
	fSettings.click_speed = 500000;
}


MouseDevice::~MouseDevice()
{
	CALLED();
	TRACE("delete\n");

	// _Classify() may have opened handles before Start() ever ran.
	Stop();

	free(fDeviceRef.name);
	delete fTouchpadSettingsMessage;
}


status_t
MouseDevice::_Classify()
{
	CALLED();

	// Called once from _AddDevice() before the device is registered.
	int fd = open(fPath.String(), O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		// udev widens permissions shortly after the node appears;
		// report distinctly so AddOnManager retries.
		return (errno == EACCES || errno == EPERM)
			? B_PERMISSION_DENIED : B_ERROR;
	}

	if (_IsGestureDevice(fd)) {
		struct stat st;
		if (fstat(fd, &st) == 0)
			fDeviceInode = st.st_ino;
		close(fd);

		fUseLibinput = true;

		fLibinput = libinput_path_create_context(&kLibinputInterface, NULL);
		if (fLibinput == NULL) {
			LOG_ERROR("MouseDevice: libinput_path_create_context failed for %s\n",
				fPath.String());
			fUseLibinput = false;
			return B_ERROR;
		}

		fLibinputDev = libinput_path_add_device(fLibinput, fPath.String());
		if (fLibinputDev == NULL) {
			LOG_ERROR("MouseDevice: libinput_path_add_device failed for %s\n",
				fPath.String());
			libinput_unref(fLibinput);
			fLibinput = NULL;
			fUseLibinput = false;
			return B_ERROR;
		}

		libinput_device_ref(fLibinputDev);

		// libinput needs one dispatch before device config is accessible.
		libinput_dispatch(fLibinput);

		if (libinput_device_config_tap_get_finger_count(
				fLibinputDev) > 0) {
			libinput_device_config_tap_set_enabled(fLibinputDev,
				LIBINPUT_CONFIG_TAP_ENABLED);
		}

		// Raw touchscreens never fire POINTER_BUTTON, so synthesize from TOUCH.
		fLibinputIsDirectTouch =
			libinput_device_has_capability(fLibinputDev,
				LIBINPUT_DEVICE_CAP_TOUCH) &&
			!libinput_device_has_capability(fLibinputDev,
				LIBINPUT_DEVICE_CAP_POINTER);

		return B_OK;
	}

	struct libevdev* evdev = NULL;
	if (libevdev_new_from_fd(fd, &evdev) < 0) {
		close(fd);
		return B_BAD_TYPE;
	}

	struct stat st;
	if (fstat(fd, &st) == 0)
		fDeviceInode = st.st_ino;

	// REL_X alone is a pointer: a trackpoint pass-through can lack
	// BTN_LEFT on resume re-probe, and that must not drop the device.
	bool isRelMouse = libevdev_has_event_type(evdev, EV_REL)
		&& libevdev_has_event_code(evdev, EV_REL, REL_X);
	bool isAbsMouse = !isRelMouse
		&& libevdev_has_event_type(evdev, EV_ABS)
		&& libevdev_has_event_code(evdev, EV_ABS, ABS_X)
		&& libevdev_has_event_code(evdev, EV_KEY, BTN_LEFT);

	if (!isRelMouse && !isAbsMouse) {
		libevdev_free(evdev);
		close(fd);
		return B_BAD_TYPE;
	}

	if (isAbsMouse) {
		fIsAbsolute = true;
		int mx = libevdev_get_abs_maximum(evdev, ABS_X);
		int my = libevdev_get_abs_maximum(evdev, ABS_Y);
		if (mx > 0) fAbsMaxX = mx;
		if (my > 0) fAbsMaxY = my;
		// A tablet's axis need not start at zero, and the span is what
		// the report scales against, not the maximum on its own.
		fAbsMinX = libevdev_get_abs_minimum(evdev, ABS_X);
		fAbsMinY = libevdev_get_abs_minimum(evdev, ABS_Y);
		fAbsResX = libevdev_get_abs_resolution(evdev, ABS_X);
		fAbsResY = libevdev_get_abs_resolution(evdev, ABS_Y);

		// INPUT_PROP_BUTTONPAD/BTN_TOOL_FINGER separates a clickpad
		// from a real tablet.
		fIsAbsoluteTouchpad =
			libevdev_has_property(evdev, INPUT_PROP_BUTTONPAD)
			|| libevdev_has_event_code(evdev, EV_KEY,
				BTN_TOOL_FINGER);
	}

	// Clickpads, synaptics absolute pads and udev-classified touchpads
	// all need the touchpad settings path and the touchpad subtype.
	{
		int32 role = UDEV_ROLE_UNKNOWN;
		BString model;
		udev_device_name(fPath.String(), model, role);
		fIsTouchpad = fIsAbsoluteTouchpad
			|| role == UDEV_ROLE_TOUCHPAD
			|| model.IFindFirst("touchpad") >= 0
			|| model.IFindFirst("trackpad") >= 0
			|| fPath.IFindFirst("touchpad") >= 0
			|| fPath.IFindFirst("trackpad") >= 0;
	}

	fEvdevHandle = evdev;
	fDevice = fd;

	return B_OK;
}


status_t
MouseDevice::Start()
{
	CALLED();

	// Re-acquire only if Stop() released the handles, or the thread
	// spawns onto fDevice == -1.
	if (fDevice < 0 && !fUseLibinput) {
		status_t classifyStatus = _Classify();
		if (classifyStatus != B_OK)
			return classifyStatus;
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
		if (find_thread(NULL) == fThread) {
			// Stop() reached via this control thread's own cleanup path
			// (self removal through fTarget._RemoveDevice()); joining
			// ourselves here would deadlock forever, and the thread is
			// already unwinding back to its own return statement.
		} else {
			suspend_thread(fThread);
			resume_thread(fThread);

			status_t dummy;
			wait_for_thread(fThread, &dummy);
		}
	}
}


status_t
MouseDevice::UpdateSettings()
{
	CALLED();

	if (fThread < 0)
		return B_ERROR;

	atomic_set(&fUpdateSettings, 1);

	return B_OK;
}


void
MouseDevice::HandleSeatMessage(uint32 what)
{
	atomic_set(&fSeatCommand, (int32)what);
}


status_t
MouseDevice::UpdateTouchpadSettings(const BMessage* message)
{
	if (!fIsTouchpad)
		return B_BAD_TYPE;
	if (fThread < 0)
		return B_ERROR;

	BAutolock _(fTouchpadSettingsLock);

	atomic_set(&fUpdateSettings, 1);

	delete fTouchpadSettingsMessage;
	fTouchpadSettingsMessage = new BMessage(*message);
	if (fTouchpadSettingsMessage == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


void
MouseDevice::UpdateScreenBounds(BRect frame, int32 orientation,
	int32 reflection)
{
	fScreenW = (int32)(frame.Width() + 1);
	fScreenH = (int32)(frame.Height() + 1);
	fOrientation = orientation;
	fReflection = reflection;

	_UpdateTouchpadScale();

	fCursorPosition.x = std::min(fCursorPosition.x, (float)(fScreenW - 1));
	fCursorPosition.y = std::min(fCursorPosition.y, (float)(fScreenH - 1));
	fLibinputLastPos.x = std::min(fLibinputLastPos.x, (float)(fScreenW - 1));
	fLibinputLastPos.y = std::min(fLibinputLastPos.y, (float)(fScreenH - 1));
}


status_t
MouseDevice::GetDescription(BMessage* message) const
{
	if (message == NULL)
		return B_BAD_VALUE;

	const char* name = NULL;
	if (fEvdevHandle != NULL)
		name = libevdev_get_name(fEvdevHandle);
	else if (fUseLibinput && fLibinputDev != NULL)
		name = libinput_device_get_name(fLibinputDev);

	if (name == NULL || name[0] == '\0')
		return B_NAME_NOT_FOUND;

	status_t status = message->AddString("description", name);
	if (status != B_OK)
		return status;

	BString model;
	int32 role = UDEV_ROLE_UNKNOWN;
	udev_device_name(fPath.String(), model, role);
	if (!model.IsEmpty())
		message->AddString("short_description", model);
	message->AddInt32("role", role);

	return B_OK;
}


static int32
device_index_from_name(const BString& name)
{
	const char* digits = name.String();
	digits += strcspn(digits, "0123456789");

	return *digits != '\0' ? atoi(digits) : 0;
}


char*
MouseDevice::_BuildShortName() const
{
	BString string(fPath);
	BString deviceName;
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(deviceName, slash + 1, string.Length() - slash);
	int32 index = device_index_from_name(deviceName) + 1;

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
	_UpdateTouchpadScale();
	if (fTarget.fCursorLock.Lock()) {
		if (fTarget.fCursorPosition.x < 0)
			fTarget.fCursorPosition.Set(fScreenW / 2.0f, fScreenH / 2.0f);
		if (!fTarget.fScreenFrame.IsValid())
			fTarget.fScreenFrame.Set(0, 0, fScreenW - 1, fScreenH - 1);
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
			// One atomic claim, so a settings change landing between
			// the test and the clear cannot be dropped.
			if (atomic_get_and_set(&fUpdateSettings, 0) != 0)
				_UpdateSettings();

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
			// One atomic claim, so a settings change landing between
			// the test and the clear cannot be dropped.
			if (atomic_get_and_set(&fUpdateSettings, 0) != 0)
				_UpdateSettings();

			// B_SEAT_DISABLED forces every button up like a real release, so nothing stays down across a VT
			// switch. B_SEAT_ENABLED/B_SYSTEM_RESUMED re-read the hardware state to catch a dropped release.
			bool forceFlush = false;
			int32 seatCmd = atomic_get_and_set(&fSeatCommand, 0);
			if (seatCmd == (int32)B_SEAT_DISABLED) {
				currentButtons = 0;
				forceFlush = true;
			} else if (seatCmd == (int32)B_SEAT_ENABLED
				|| seatCmd == (int32)B_SYSTEM_RESUMED) {
				static const struct { int code; uint32 bit; }
					kButtonBits[] = {
						{ BTN_LEFT, 0x01 }, { BTN_RIGHT, 0x02 },
						{ BTN_MIDDLE, 0x04 }, { BTN_SIDE, 0x08 },
						{ BTN_EXTRA, 0x10 }
					};
				currentButtons = 0;
				for (const auto& button : kButtonBits) {
					if (libevdev_get_event_value(fEvdevHandle, EV_KEY,
							button.code) > 0)
						currentButtons |= button.bit;
				}
				forceFlush = true;
			}

			struct epoll_event fired;
			int n = forceFlush ? 1 : epoll_wait(fEpollFd, &fired, 1, 100);
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
			bool touchChanged = false;
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
					// The sync brought libevdev's state up to date, so take the buttons from it;
					// otherwise a release dropped meanwhile (e.g. across suspend) leaves a button stuck down.
					static const struct { int code; uint32 bit; }
						kButtonBits[] = {
							{ BTN_LEFT, 0x01 }, { BTN_RIGHT, 0x02 },
							{ BTN_MIDDLE, 0x04 }, { BTN_SIDE, 0x08 },
							{ BTN_EXTRA, 0x10 }
						};
					currentButtons = 0;
					for (const auto& button : kButtonBits) {
						if (libevdev_get_event_value(fEvdevHandle, EV_KEY,
								button.code) > 0)
							currentButtons |= button.bit;
					}
					if (fIsAbsolute) {
						currentAbsX = libevdev_get_event_value(fEvdevHandle,
							EV_ABS, ABS_X);
						currentAbsY = libevdev_get_event_value(fEvdevHandle,
							EV_ABS, ABS_Y);
					}
					continue;
				}
				if (rc < 0) {
					// Not -EAGAIN: treating it as such spun epoll
					// forever on a level-triggered EPOLLHUP.
					_ControlThreadCleanup();
					return;
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
					if (iev.code == BTN_TOOL_FINGER || iev.code == BTN_TOUCH)
						touchChanged = true;
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
			if (fIsAbsolute && !fIsAbsoluteTouchpad && currentAbsX >= 0) {
				// Scale onto the last pixel, not the pixel count, or full
				// deflection lands one column past the right edge.
				int32 spanX = fAbsMaxX - fAbsMinX;
				int32 spanY = fAbsMaxY - fAbsMinY;
				if (spanX > 0 && spanY > 0) {
					// Normalize into physical panel space, rotate into
					// logical (landscape) space, then scale.
					float u = (float)(currentAbsX - fAbsMinX) / spanX;
					float v = (float)(currentAbsY - fAbsMinY) / spanY;
					float outU, outV;
					_RotatePoint(u, v, outU, outV);
					fCursorPosition.x = outU * (fScreenW - 1);
					fCursorPosition.y = outV * (fScreenH - 1);
				}

				fCursorPosition.x = std::max(0.0f,
					std::min((float)(fScreenW - 1), fCursorPosition.x));
				fCursorPosition.y = std::max(0.0f,
					std::min((float)(fScreenH - 1), fCursorPosition.y));
				fLastAbsX = currentAbsX;
				fLastAbsY = currentAbsY;
				xdelta = 1; // non-zero to trigger movement message
				ydelta = 0;
			} else {
				// Refresh unconditionally: another device on the same seat
				// may own all the motion while this one only reports buttons.
				if (fTarget.fCursorLock.Lock()) {
					fCursorPosition = fTarget.fCursorPosition;
					fTarget.fCursorLock.Unlock();
				}

				if (fIsAbsoluteTouchpad) {
					// A touchpad reports finger position on the pad, not screen position.
					// A touch-down only sets the reference, or the cursor jumps.
					int touchCode = libevdev_has_event_code(fEvdevHandle,
						EV_KEY, BTN_TOUCH) ? BTN_TOUCH : BTN_TOOL_FINGER;
					if (libevdev_get_event_value(fEvdevHandle, EV_KEY,
							touchCode) == 0) {
						fLastAbsX = -1;
						fLastAbsY = -1;
						fTouchpadResidualX = 0.0f;
						fTouchpadResidualY = 0.0f;
					} else {
						int32 x = libevdev_get_event_value(fEvdevHandle,
							EV_ABS, ABS_X);
						int32 y = libevdev_get_event_value(fEvdevHandle,
							EV_ABS, ABS_Y);
						if (fLastAbsX >= 0 && !touchChanged) {
							// Pad units are not screen pixels; scale first so
							// speed and acceleration act on pointer motion.
							float sx = fTouchpadResidualX
								+ (float)(x - fLastAbsX) * fTouchpadScaleX;
							float sy = fTouchpadResidualY
								+ (float)(y - fLastAbsY) * fTouchpadScaleY;
							int32 dx = (int32)(sx < 0 ? sx - 0.5f : sx + 0.5f);
							int32 dy = (int32)(sy < 0 ? sy - 0.5f : sy + 0.5f);
							fTouchpadResidualX = sx - (float)dx;
							fTouchpadResidualY = sy - (float)dy;
							xdelta += dx;
							ydelta += dy;
						}
						fLastAbsX = x;
						fLastAbsY = y;
					}
				}

				if (xdelta != 0 || ydelta != 0) {
					float rx, ry;
					_RotateDelta((float)xdelta,
						(float)ydelta, rx, ry);
					xdelta = (int32)rx;
					ydelta = (int32)ry;

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
			}
			// Write back to shared slot so the next device starts from here.
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fCursorPosition = fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			bool hasMoved = (fIsAbsolute && !fIsAbsoluteTouchpad
					&& fLastAbsX >= 0)
				|| (xdelta != 0 || ydelta != 0);
			uint32 changedButtons = lastButtons ^ currentButtons;

			if (!hasMoved && changedButtons == 0
					&& wheel_xdelta == 0 && wheel_ydelta == 0)
				continue;

			uint32 remappedButtons = _RemapButtons(currentButtons);

			// Report the union so this device's move doesn't drop a
			// button another device is holding.
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fButtons = (fTarget.fButtons & ~fSharedButtons)
					| remappedButtons;
				fSharedButtons = remappedButtons;
				remappedButtons = fTarget.fButtons;
				fTarget.fCursorLock.Unlock();
			}

			// Button events
			if (changedButtons != 0) {
				bool pressed = (changedButtons & currentButtons) != 0;
				BMessage* message = _BuildMouseMessage(
					pressed ? B_MOUSE_DOWN : B_MOUSE_UP,
					timestamp, remappedButtons);
				if (message != NULL) {
					if (pressed) {
						if (remappedButtons == fLastClickButtons
							&& fLastClickTime + fSettings.click_speed
								> timestamp)
							fClickCount++;
						else
							fClickCount = 1;

						fLastClickTime = timestamp;
						fLastClickButtons = remappedButtons;
						message->AddInt32("clicks", fClickCount);
					}
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
				float wx, wy;
				_RotateDelta((float)wheel_xdelta,
					(float)wheel_ydelta, wx, wy);

				BMessage* message = new BMessage(B_MOUSE_WHEEL_CHANGED);
				if (message != NULL) {
					message->AddInt64("when", timestamp);
					message->AddFloat("be:wheel_delta_x", wx);
					message->AddFloat("be:wheel_delta_y", wy);
					fTarget.EnqueueMessage(message);
				}
			}
		}
	}
}


void
MouseDevice::_ControlThreadCleanup()
{
	// Withdraw our bits first: unplugging a device mid-click would
	// otherwise leave a button stuck down for everyone else.
	if (fSharedButtons != 0 && fTarget.fCursorLock.Lock()) {
		fTarget.fButtons &= ~fSharedButtons;
		fSharedButtons = 0;
		fTarget.fCursorLock.Unlock();
	}

	// Do NOT pre-clear fThread here: Stop() (invoked from the delete this
	// triggers, on whichever thread ends up performing it) tells self-
	// removal apart from external removal by comparing fThread against
	// find_thread(), not by a flag on `this`. That keeps `this` valid for
	// the snapshot below no matter which thread wins the race, since a
	// joining Stop() can only return once this thread has truly finished.

	if (fActive) {
		BString path(fPath);
		// Serial, not path: a fast replug may already have swapped in
		// a replacement.
		int32 serial = fSerial;
		fTarget._RemoveDevice(path.String(), serial);
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

	// Gesture settings arrive as a pending message; apply them here so
	// they land on the same settings refresh as speed and click rate.
	if (fIsTouchpad) {
		BAutolock locker(fTouchpadSettingsLock);
		if (fTouchpadSettingsMessage != NULL) {
			_UpdateTouchpadSettings(fTouchpadSettingsMessage);
			delete fTouchpadSettingsMessage;
			fTouchpadSettingsMessage = NULL;
		}
	}
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


// A full pad width covers the screen width at speed 1.0; the slider
// then multiplies those screen-pixel deltas like any other pointer.
void
MouseDevice::_UpdateTouchpadScale()
{
	fTouchpadScaleX = 0.0f;
	fTouchpadScaleY = 0.0f;
	if (!fIsAbsoluteTouchpad)
		return;

	int32 spanX = fAbsMaxX - fAbsMinX;
	int32 spanY = fAbsMaxY - fAbsMinY;
	if (spanX <= 0 || spanY <= 0 || fScreenW <= 1 || fScreenH <= 1)
		return;

	// One scale for both axes, so a diagonal stroke stays diagonal; Y
	// follows X through the pad's own units per mm when it reports them.
	fTouchpadScaleX = (float)(fScreenW - 1) / (float)spanX;
	fTouchpadScaleY = fTouchpadScaleX;
	if (fAbsResX > 0 && fAbsResY > 0)
		fTouchpadScaleY = fTouchpadScaleX * fAbsResX / fAbsResY;
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


BPoint
MouseDevice::_TouchPoint(struct libinput_event_touch* tev) const
{
	float u = (float)libinput_event_touch_get_x_transformed(tev, 1);
	float v = (float)libinput_event_touch_get_y_transformed(tev, 1);
	float outU, outV;
	_RotatePoint(u, v, outU, outV);
	return BPoint(outU * (fScreenW - 1), outV * (fScreenH - 1));
}


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

			double rawDx = libinput_event_pointer_get_dx(pev);
			double rawDy = libinput_event_pointer_get_dy(pev);
			float dx, dy;
			_RotateDelta((float)rawDx, (float)rawDy,
				dx, dy);

			// Sync from shared slot before applying delta
			if (fTarget.fCursorLock.Lock()) {
				fLibinputLastPos = fTarget.fCursorPosition;
				fTarget.fCursorLock.Unlock();
			}

			fLibinputLastPos.x += dx;
			fLibinputLastPos.y += dy;
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
			msg->AddInt32("buttons", (int32)_RemapButtons(fLibinputButtons));
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

			// Normalize into physical panel space (width=1, height=1),
			// rotate into logical space, then scale to the screen.
			double u = libinput_event_pointer_get_absolute_x_transformed(
				pev, 1);
			double v = libinput_event_pointer_get_absolute_y_transformed(
				pev, 1);
			float outU, outV;
			_RotatePoint((float)u, (float)v, outU, outV);

			fLibinputLastPos.x = outU * (fScreenW - 1);
			fLibinputLastPos.y = outV * (fScreenH - 1);

			// Sync shared cursor position
			if (fTarget.fCursorLock.Lock()) {
				fTarget.fCursorPosition = fLibinputLastPos;
				fTarget.fCursorLock.Unlock();
			}

			BMessage* msg = new BMessage(B_MOUSE_MOVED);
			msg->AddPoint("where", fLibinputLastPos);
			msg->AddInt32("buttons", (int32)_RemapButtons(fLibinputButtons));
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
			msg->AddInt32("buttons", (int32)_RemapButtons(fLibinputButtons));
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_TOUCHPAD_POINTING_DEVICE);
			if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
				bigtime_t now = system_time();
				if (beButton == fLibinputLastClickButton
					&& (now - fLibinputLastClickTime) < fSettings.click_speed)
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
			float scrollDx, scrollDy;
			_RotateDelta((float)rawDx, (float)rawDy,
				scrollDx, scrollDy);

			fLibinputScrollAccX += scrollDx;
			fLibinputScrollAccY += scrollDy;

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

			double rawDx = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
			double rawDy = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
			float scrollDx, scrollDy;
			_RotateDelta((float)rawDx, (float)rawDy,
				scrollDx, scrollDy);

			fLibinputScrollAccX += scrollDx;
			fLibinputScrollAccY += scrollDy;

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
					BPoint p = _TouchPoint(tev);
					float sx = p.x, sy = p.y;
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
					BPoint p = _TouchPoint(tev);
					float sx = p.x, sy = p.y;
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
				fLibinputLastPos = _TouchPoint(tev);

				if (fTarget.fCursorLock.Lock()) {
					fTarget.fCursorPosition = fLibinputLastPos;
					fTarget.fCursorLock.Unlock();
				}

				// Touchscreen: synthesize B_MOUSE_DOWN.
				// Touchpad: B_MOUSE_DOWN comes from POINTER_BUTTON (tap-to-click).
				if (fLibinputIsDirectTouch) {
					fLibinputTouchSlot0Down = true;

					// A finger arrives without ever having moved there.
					// Menu tracking follows the pointer rather than the
					// message, so it needs the move before the press.
					BMessage* move = new BMessage(B_MOUSE_MOVED);
					move->AddPoint("where", fLibinputLastPos);
					move->AddInt32("buttons", 0);
					move->AddInt32("modifiers", 0);
					move->AddInt64("when", system_time());
					move->AddInt32("be:device_subtype",
						B_TOUCHPAD_POINTING_DEVICE);
					fTarget.EnqueueMessage(move);

					uint32 tapButton = _RemapButtons(0x1);
					BMessage* msg = new BMessage(B_MOUSE_DOWN);
					msg->AddPoint("where", fLibinputLastPos);
					msg->AddInt32("buttons", (int32)tapButton);
					msg->AddInt32("modifiers", 0);
					msg->AddInt64("when", system_time());
					msg->AddInt32("clicks", 1);
					msg->AddInt32("be:button", (int32)tapButton);
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

				fLibinputLastPos = _TouchPoint(tev);

				if (fTarget.fCursorLock.Lock()) {
					fTarget.fCursorPosition = fLibinputLastPos;
					fTarget.fCursorLock.Unlock();
				}

				// For touchscreen: emit B_MOUSE_MOVED with button held (drag).
				// For touchpad: only emit when no pointer button is held.
				uint32 moveBtns = 0;
				bool shouldEmit = false;
				if (fLibinputIsDirectTouch) {
					moveBtns = fLibinputTouchSlot0Down
						? _RemapButtons(0x1) : 0;
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
	fCursorLock("cursor position lock"),
	fButtons(0),
	fOrientation(B_PANEL_ORIENTATION_NORMAL),
	fReflection(B_PANEL_REFLECTION_NONE)
{
	CALLED();

	StartMonitoringDevice(kMouseDevicesDirectory);
	_RecursiveScan(kMouseDevicesDirectory);
}


MouseInputDevice::~MouseInputDevice()
{
	CALLED();

	StopMonitoringDevice(kMouseDevicesDirectory);

	// Detach every device under the lock, then delete them outside it:
	// ~MouseDevice() may join a control thread via Stop(), and joining
	// while fDeviceListLock is held can deadlock against that same
	// thread's own self-removal call (see _ControlThreadCleanup()).
	BObjectList<MouseDevice, false> doomed;
	{
		BAutolock _(fDeviceListLock);
		for (int32 i = fDevices.CountItems() - 1; i >= 0; i--)
			doomed.AddItem(fDevices.ItemAt(i));
		fDevices.MakeEmpty(false);
	}

	// Unregister before deleting: ~BInputServerDevice() does it too, but
	// a base destructor runs after this one, so input_server would still
	// hold cookies pointing at freed devices in between.
	for (int32 i = 0; i < doomed.CountItems(); i++) {
		MouseDevice* device = doomed.ItemAt(i);

		input_device_ref* devices[2];
		devices[0] = device->DeviceRef();
		devices[1] = NULL;
		UnregisterDevices(devices);

		delete device;
	}
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
	if (device == NULL)
		return B_BAD_VALUE;

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

	if (command == B_GET_DEVICE_DESCRIPTION)
		return device->GetDescription(message);

	if (command == B_SCREEN_BOUNDS_CHANGED)
		return _UpdateScreenBounds(device, message);

	if (command == B_SEAT_DISABLED || command == B_SEAT_ENABLED
		|| command == B_SYSTEM_RESUMED) {
		// Add-on level: resume can recreate evdev nodes that the node
		// monitor missed; reconcile against what is on disk now.
		if (cookie == NULL) {
			if (command == B_SYSTEM_RESUMED)
				_RescanDevices();
			return B_OK;
		}
		device->HandleSeatMessage(command);
		return B_OK;
	}

	if (command >= B_MOUSE_TYPE_CHANGED
		&& command <= B_MOUSE_ACCELERATION_CHANGED)
		return device->UpdateSettings();

	return B_BAD_VALUE;
}


status_t
MouseInputDevice::_UpdateScreenBounds(MouseDevice* device, BMessage* message)
{
	BRect frame;
	if (device == NULL || message == NULL
		|| message->FindRect("screen_bounds", &frame) != B_OK)
		return B_BAD_VALUE;

	// Absent on older app_server builds; normal orientation either way.
	int32 orientation = B_PANEL_ORIENTATION_NORMAL;
	message->FindInt32("screen_orientation", &orientation);
	int32 reflection = B_PANEL_REFLECTION_NONE;
	message->FindInt32("screen_reflection", &reflection);

	// Rescale only the first time this frame is seen (once per device call).
	if (fCursorLock.Lock()) {
		if (frame != fScreenFrame && fScreenFrame.IsValid()
			&& fCursorPosition.x >= 0) {
			fCursorPosition.x = fCursorPosition.x * frame.Width()
				/ fScreenFrame.Width();
			fCursorPosition.y = fCursorPosition.y * frame.Height()
				/ fScreenFrame.Height();
		}
		fScreenFrame = frame;
		fOrientation = orientation;
		fReflection = reflection;
		fCursorLock.Unlock();
	}

	device->UpdateScreenBounds(frame, orientation, reflection);
	return B_OK;
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


void
MouseInputDevice::_RescanDevices()
{
	CALLED();

	BEntry entry;
	BDirectory dir(kMouseDevicesDirectory);
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
				MouseDevice* device = fDevices.ItemAt(i);
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

	// Drop devices whose node vanished; their fds are stale.
	BObjectList<BString, true> paths;
	{
		BAutolock _(fDeviceListLock);
		for (int32 i = 0; i < fDevices.CountItems(); i++)
			paths.AddItem(new BString(fDevices.ItemAt(i)->Path()));
	}
	for (int32 i = 0; i < paths.CountItems(); i++) {
		BString* devicePath = paths.ItemAt(i);
		struct stat st;
		if (stat(devicePath->String(), &st) != 0)
			_RemoveDevice(devicePath->String());
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

	// The node monitor hands us any entry under /dev/input, not just
	// device nodes.
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
		return B_BAD_TYPE;

	// Check readiness first, or a node still in the udev window
	// registers and never produces events.
	if (access(path, R_OK) != 0 && (errno == EACCES || errno == EPERM))
		return B_PERMISSION_DENIED;

	// Detach and delete any stale device already at this path (fast
	// replug) before the lock below is taken: _RemoveDevice() may join
	// that device's control thread via Stop(), and that must happen
	// without fDeviceListLock held, see _DetachDevice().
	_RemoveDevice(path);

	BAutolock _(fDeviceListLock);

	// Rescan and node monitor race; a duplicate would never be started.
	if (_FindDevice(path) != NULL)
		return B_OK;

	MouseDevice* device = new(std::nothrow) MouseDevice(*this, path);
	if (device == NULL) {
		TRACE("No memory\n");
		return B_NO_MEMORY;
	}

	// Classify before registering, so non-pointing devices never
	// reach Input preferences.
	status_t classifyStatus = device->_Classify();
	if (classifyStatus != B_OK) {
		delete device;
		return classifyStatus;
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


MouseDevice*
MouseInputDevice::_DetachDevice(const char* path, const int32* serial)
{
	CALLED();

	MouseDevice* device = NULL;
	{
		BAutolock _(fDeviceListLock);

		device = _FindDevice(path);
		if (device == NULL) {
			TRACE("%s not found\n", path);
			return NULL;
		}

		// ABA guard: a replacement can land on the same address, so
		// serial, not pointer.
		if (serial != NULL && device->Serial() != *serial) {
			TRACE("%s: stale removal (serial %ld != %ld), ignoring\n", path,
				(long)*serial, (long)device->Serial());
			return NULL;
		}

		// Detach only, don't delete: the caller deletes outside the lock,
		// so ~MouseDevice()'s Stop() can join the control thread without
		// holding fDeviceListLock. Holding it there would deadlock
		// against the control thread's own self-removal call into
		// _RemoveDevice(), which needs the same lock (see
		// _ControlThreadCleanup()).
		fDevices.RemoveItem(device, false);
	}

	// Outside the lock for two reasons. UnregisterDevices() makes
	// input_server call BInputServerDevice::Stop(), which joins the
	// control thread; that join must not happen under fDeviceListLock or
	// it deadlocks against the same thread's self-removal. And it takes
	// InputServer::fInputDeviceListLocker, which input_server already
	// holds when it calls into Control() (InputServer.cpp:1496,:1504),
	// so acquiring it under fDeviceListLock inverts the established
	// order. The device is off the list but still alive here, so a
	// callback arriving with the cookie in this window is safe.
	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	TRACE("removing path: %s, name: %s\n", path, devices[0]->name);

	UnregisterDevices(devices);

	return device;
}


status_t
MouseInputDevice::_RemoveDevice(const char* path)
{
	MouseDevice* device = _DetachDevice(path, NULL);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	delete device;
	return B_OK;
}


status_t
MouseInputDevice::_RemoveDevice(const char* path, int32 serial)
{
	MouseDevice* device = _DetachDevice(path, &serial);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	delete device;
	return B_OK;
}
