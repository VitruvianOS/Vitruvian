/*
 * Copyright 2024-2026, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */

#include "LibEvdevEventStream.h"

#include "LinuxKeycodeMap.h"
#include "PanelOrientationTransform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <InterfaceDefs.h>
#include <View.h>
#include <Autolock.h>


template<typename T>
constexpr const T& clamp_constref(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}


static const struct {
	uint32 linuxKey;
	uint32 Bkey;
	uint32 shifted;
} kKeyMap[] = {
	{ KEY_ESC, B_ESCAPE, 0 },
	{ KEY_1, '1', '!' },
	{ KEY_2, '2', '@' },
	{ KEY_3, '3', '#' },
	{ KEY_4, '4', '$' },
	{ KEY_5, '5', '%' },
	{ KEY_6, '6', '^' },
	{ KEY_7, '7', '&' },
	{ KEY_8, '8', '*' },
	{ KEY_9, '9', '(' },
	{ KEY_0, '0', ')' },
	{ KEY_MINUS, '-', '_' },
	{ KEY_EQUAL, '=', '+' },
	{ KEY_BACKSPACE, B_BACKSPACE, 0 },
	{ KEY_TAB, B_TAB, 0 },
	{ KEY_Q, 'q', 'Q' },
	{ KEY_W, 'w', 'W' },
	{ KEY_E, 'e', 'E' },
	{ KEY_R, 'r', 'R' },
	{ KEY_T, 't', 'T' },
	{ KEY_Y, 'y', 'Y' },
	{ KEY_U, 'u', 'U' },
	{ KEY_I, 'i', 'I' },
	{ KEY_O, 'o', 'O' },
	{ KEY_P, 'p', 'P' },
	{ KEY_LEFTBRACE, '[', '{' },
	{ KEY_RIGHTBRACE, ']', '}' },
	{ KEY_ENTER, B_ENTER, 0 },
	{ KEY_A, 'a', 'A' },
	{ KEY_S, 's', 'S' },
	{ KEY_D, 'd', 'D' },
	{ KEY_F, 'f', 'F' },
	{ KEY_G, 'g', 'G' },
	{ KEY_H, 'h', 'H' },
	{ KEY_J, 'j', 'J' },
	{ KEY_K, 'k', 'K' },
	{ KEY_L, 'l', 'L' },
	{ KEY_SEMICOLON, ';', ':' },
	{ KEY_APOSTROPHE, '\'', '"' },
	{ KEY_GRAVE, '`', '~' },
	{ KEY_BACKSLASH, '\\', '|' },
	{ KEY_Z, 'z', 'Z' },
	{ KEY_X, 'x', 'X' },
	{ KEY_C, 'c', 'C' },
	{ KEY_V, 'v', 'V' },
	{ KEY_B, 'b', 'B' },
	{ KEY_N, 'n', 'N' },
	{ KEY_M, 'm', 'M' },
	{ KEY_COMMA, ',', '<' },
	{ KEY_DOT, '.', '>' },
	{ KEY_SLASH, '/', '?' },
	{ KEY_KPASTERISK, '*', 0 },
	{ KEY_SPACE, B_SPACE, 0 },
	{ KEY_F1, B_FUNCTION_KEY, 0 },
	{ KEY_F2, B_FUNCTION_KEY, 0 },
	{ KEY_F3, B_FUNCTION_KEY, 0 },
	{ KEY_F4, B_FUNCTION_KEY, 0 },
	{ KEY_F5, B_FUNCTION_KEY, 0 },
	{ KEY_F6, B_FUNCTION_KEY, 0 },
	{ KEY_F7, B_FUNCTION_KEY, 0 },
	{ KEY_F8, B_FUNCTION_KEY, 0 },
	{ KEY_F9, B_FUNCTION_KEY, 0 },
	{ KEY_F10, B_FUNCTION_KEY, 0 },
	{ KEY_F11, B_FUNCTION_KEY, 0 },
	{ KEY_F12, B_FUNCTION_KEY, 0 },
	{ KEY_KPMINUS, '-', '_' },
	{ KEY_KPPLUS, '+', 0 },
	{ KEY_KPDOT, '.', '>' },
	{ KEY_102ND, '\\', '|' },
	{ KEY_HOME, B_HOME, 0 },
	{ KEY_UP, B_UP_ARROW, 0 },
	{ KEY_PAGEUP, B_PAGE_UP, 0 },
	{ KEY_LEFT, B_LEFT_ARROW, 0 },
	{ KEY_RIGHT, B_RIGHT_ARROW, 0 },
	{ KEY_END, B_END, 0 },
	{ KEY_DOWN, B_DOWN_ARROW, 0 },
	{ KEY_PAGEDOWN, B_PAGE_DOWN, 0 },
	{ KEY_INSERT, B_INSERT, 0 },
	{ KEY_DELETE, B_DELETE, 0 },
};


static uint32 MapMouseButton(uint32 linuxButton)
{
	switch (linuxButton) {
		case BTN_LEFT: return B_PRIMARY_MOUSE_BUTTON;
		case BTN_RIGHT: return B_SECONDARY_MOUSE_BUTTON;
		case BTN_MIDDLE: return B_TERTIARY_MOUSE_BUTTON;
		case BTN_SIDE: return 0x08;
		case BTN_EXTRA: return 0x10;
		default: return 0;
	}
}


LibEvdevEventStream::LibEvdevEventStream(uint32 width, uint32 height,
	struct libseat* seat, int32 orientation, int32 reflection)
	:
	fEventList(10),
	fEventListLocker("evdev event list"),
	fEventNotification(-1),
	fWaitingOnEvent(false),
	fLatestMouseMovedEvent(NULL),
	fMousePosition(width / 2, height / 2),
	fPendingMouseDelta(0, 0),
	fLastAbsU(0.5f),
	fLastAbsV(0.5f),
	fMouseButtons(0),
	fModifiers(0),
	fOldModifiers(0),
	fMouseMoved(false),
	fCapsLock(false),
	fNumLock(false),
	fScrollLock(false),
	fRunning(true),
	fSuspended(false),
	fWidth(width),
	fHeight(height),
	fOrientation(orientation >= 0 ? orientation : B_PANEL_ORIENTATION_NORMAL),
	fReflection(reflection >= 0 ? reflection : B_PANEL_REFLECTION_NONE),
	fSeat(seat),
	fEpollFd(-1)
{
	memset(fKeyStates, 0, sizeof(fKeyStates));

	fEventNotification = create_sem(0, "evdev event notification");

	fEpollFd = epoll_create1(0);
	if (fEpollFd < 0) {
		fprintf(stderr, "[LibEvdevEventStream] Failed to create epoll\n");
		return;
	}

	_ScanDevices();

	if (fDevices.empty()) {
		fprintf(stderr, "[LibEvdevEventStream] No input devices found\n");
		close(fEpollFd);
		fEpollFd = -1;
		return;
	}

	thread_id pollThread = spawn_thread(_PollEventsThread, "evdev events",
		B_NORMAL_PRIORITY, (void*)this);
	resume_thread(pollThread);

	fprintf(stderr, "[LibEvdevEventStream] Initialized with %zu devices, libseat=%p\n",
		fDevices.size(), (void*)seat);
}


LibEvdevEventStream::~LibEvdevEventStream()
{
	fRunning = false;

	delete_sem(fEventNotification);

	for (auto& dev : fDevices) {
		_CloseDevice(dev);
	}
	fDevices.clear();

	if (fEpollFd >= 0)
		close(fEpollFd);
}


void
LibEvdevEventStream::_ScanDevices()
{
	DIR* dir = opendir("/dev/input");
	if (!dir)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "event", 5) != 0)
			continue;

		char path[PATH_MAX];
		snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

		_OpenDevice(path);
	}

	closedir(dir);
}


bool
LibEvdevEventStream::_OpenDevice(const char* path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return false;

	struct libevdev* evdev = NULL;
	int rc = libevdev_new_from_fd(fd, &evdev);
	if (rc < 0) {
		close(fd);
		return false;
	}

	// Check device capabilities
	bool isKeyboard = libevdev_has_event_type(evdev, EV_KEY) &&
					  libevdev_has_event_code(evdev, EV_KEY, KEY_A);
	bool isMouse = libevdev_has_event_type(evdev, EV_REL) &&
				   libevdev_has_event_code(evdev, EV_REL, REL_X);
	bool isPointer = libevdev_has_event_type(evdev, EV_ABS) ||
					 libevdev_has_event_code(evdev, EV_KEY, BTN_LEFT);

	if (!isKeyboard && !isMouse && !isPointer) {
		libevdev_free(evdev);
		close(fd);
		return false;
	}

	// Add to epoll
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		libevdev_free(evdev);
		close(fd);
		return false;
	}

	EvdevDevice dev;
	dev.fd = fd;
	// libseat device ids apply only to the janus-side seat open path.
	dev.seatDeviceId = -1;
	dev.evdev = evdev;
	dev.path = strdup(path);
	dev.isKeyboard = isKeyboard;
	dev.isMouse = isMouse;
	dev.isPointer = isPointer;

	fDevices.push_back(dev);

	fprintf(stderr, "[LibEvdevEventStream] Opened %s: %s (kb=%d mouse=%d ptr=%d)\n",
		path, libevdev_get_name(evdev), isKeyboard, isMouse, isPointer);

	return true;
}


void
LibEvdevEventStream::_CloseDevice(EvdevDevice& dev)
{
	if (dev.evdev) {
		libevdev_free(dev.evdev);
		dev.evdev = NULL;
	}

	if (dev.fd >= 0) {
		epoll_ctl(fEpollFd, EPOLL_CTL_DEL, dev.fd, NULL);

		if (fSeat && dev.seatDeviceId >= 0)
			libseat_close_device(fSeat, dev.seatDeviceId);
		else
			close(dev.fd);

		dev.fd = -1;
	}

	if (dev.path) {
		free((void*)dev.path);
		dev.path = NULL;
	}
}


void
LibEvdevEventStream::UpdateScreenBounds(BRect bounds)
{
	UpdateScreenBounds(bounds, fOrientation, fReflection);
}


void
LibEvdevEventStream::UpdateScreenBounds(BRect bounds, int32 orientation,
	int32 reflection)
{
	fWidth = bounds.IntegerWidth() + 1;
	fHeight = bounds.IntegerHeight() + 1;
	fOrientation = orientation;
	fReflection = reflection;
}


bool
LibEvdevEventStream::GetNextEvent(BMessage** _event)
{
	BAutolock lock(fEventListLocker);
	while (fEventList.CountItems() == 0) {
		fWaitingOnEvent = true;
		lock.Unlock();

		status_t result;
		do {
			result = acquire_sem(fEventNotification);
		} while (result == B_INTERRUPTED);

		lock.Lock();
		if (!lock.IsLocked())
			return false;
	}

	*_event = fEventList.RemoveItemAt(0);
	return true;
}


status_t
LibEvdevEventStream::InsertEvent(BMessage* event)
{
	BAutolock lock(fEventListLocker);
	if (!lock.IsLocked() || !fEventList.AddItem(event))
		return B_ERROR;

	if (event->what == B_MOUSE_MOVED)
		fLatestMouseMovedEvent = event;

	return B_OK;
}


BMessage*
LibEvdevEventStream::PeekLatestMouseMoved()
{
	return fLatestMouseMovedEvent;
}


int32
LibEvdevEventStream::_PollEventsThread(void* cookie)
{
	LibEvdevEventStream* owner = (LibEvdevEventStream*)cookie;
	owner->_PollEvents();
	return B_OK;
}


void
LibEvdevEventStream::_PollEvents()
{
	struct epoll_event events[16];

	while (fRunning) {
		if (fSuspended) {
			usleep(100000);  // 100ms
			continue;
		}

		int nfds = epoll_wait(fEpollFd, events, 16, 100);
		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (int i = 0; i < nfds; i++) {
			int fd = events[i].data.fd;

			// Find the device
			EvdevDevice* dev = NULL;
			for (auto& d : fDevices) {
				if (d.fd == fd) {
					dev = &d;
					break;
				}
			}

			if (!dev || !dev->evdev)
				continue;

			struct input_event ev;
			int rc;
			while ((rc = libevdev_next_event(dev->evdev,
					LIBEVDEV_READ_FLAG_NORMAL, &ev)) == LIBEVDEV_READ_STATUS_SUCCESS) {

				switch (ev.type) {
					case EV_KEY:
						if (ev.code >= BTN_MISC && ev.code < KEY_OK) {
							_ProcessButtonEvent(*dev, ev);
						} else {
							_ProcessKeyEvent(*dev, ev);
						}
						break;

					case EV_REL:
						_ProcessRelEvent(*dev, ev);
						break;

					case EV_ABS:
						_ProcessAbsEvent(*dev, ev);
						break;

					case EV_SYN:
						if (ev.code == SYN_REPORT) {
							_FlushPendingEvents();
						}
						break;
				}
			}

			if (rc == LIBEVDEV_READ_STATUS_SYNC) {
				// Device sync needed - handle dropped events
				while ((rc = libevdev_next_event(dev->evdev,
						LIBEVDEV_READ_FLAG_SYNC, &ev)) == LIBEVDEV_READ_STATUS_SYNC) {
					// Process sync events
				}
			}
		}
	}
}


void
LibEvdevEventStream::Suspend()
{
	if (fSuspended)
		return;

	fprintf(stderr, "[LibEvdevEventStream] Suspending input\n");
	fSuspended = true;

	// Devices are managed by libseat, no need to close them
}


void
LibEvdevEventStream::Resume()
{
	if (!fSuspended)
		return;

	fprintf(stderr, "[LibEvdevEventStream] Resuming input\n");
	fSuspended = false;
}


void
LibEvdevEventStream::_ProcessKeyEvent(EvdevDevice& dev, struct input_event& ev)
{
	uint32 keyCode = ev.code;
	bool pressed = (ev.value != 0);  // 1 = press, 2 = repeat, 0 = release

	_UpdateModifiers(keyCode, pressed);
	uint32 modifiers = _GetCurrentModifiers();

	// "key" is Haiku keycode (not character); "bytes"/"raw_char" are characters
	int32 haikuKey = linux_to_haiku_keycode(keyCode);
	if (haikuKey == 0)
		haikuKey = (int32)keyCode;
	int32 character = _MapCharacter(keyCode,
		(modifiers & B_SHIFT_KEY) != 0, fCapsLock);

	if (ev.value == 1) {  // Key press
		fKeyStates[keyCode] = true;
		_SendKeyEvent(B_KEY_DOWN, haikuKey, character, modifiers);
	} else if (ev.value == 0) {  // Key release
		fKeyStates[keyCode] = false;
		_SendKeyEvent(B_KEY_UP, haikuKey, character, modifiers);
	} else if (ev.value == 2) {  // Key repeat
		_SendKeyEvent(B_KEY_DOWN, haikuKey, character, modifiers);
	}

	if (modifiers != fOldModifiers) {
		BMessage* event = new BMessage(B_MODIFIERS_CHANGED);
		event->AddInt32("be:old_modifiers", fOldModifiers);
		event->AddInt32("modifiers", modifiers);
		event->AddInt64("when", system_time());

		BAutolock lock(fEventListLocker);
		fEventList.AddItem(event);
		if (fWaitingOnEvent) {
			fWaitingOnEvent = false;
			lock.Unlock();
			release_sem(fEventNotification);
		}

		fOldModifiers = modifiers;
	}
}


void
LibEvdevEventStream::_RotatePoint(float u, float v, float& outU,
	float& outV) const
{
	rotate_panel_point(fOrientation, u, v, outU, outV, fReflection);
}


void
LibEvdevEventStream::_ProcessRelEvent(EvdevDevice& dev, struct input_event& ev)
{
	switch (ev.code) {
		case REL_X:
			fPendingMouseDelta.x += ev.value;
			fMouseMoved = true;
			break;

		case REL_Y:
			fPendingMouseDelta.y += ev.value;
			fMouseMoved = true;
			break;

		case REL_WHEEL:
		case REL_WHEEL_HI_RES:
		{
			float delta = ev.code == REL_WHEEL_HI_RES ? ev.value / 120.0f : (float)ev.value;
			float wx = 0.0f;
			float wy = -delta;

			BMessage* event = new BMessage(B_MOUSE_WHEEL_CHANGED);
			event->AddFloat("be:wheel_delta_x", wx);
			event->AddFloat("be:wheel_delta_y", wy);
			event->AddInt64("when", system_time());

			BAutolock lock(fEventListLocker);
			fEventList.AddItem(event);
			if (fWaitingOnEvent) {
				fWaitingOnEvent = false;
				lock.Unlock();
				release_sem(fEventNotification);
			}
			break;
		}

		case REL_HWHEEL:
		case REL_HWHEEL_HI_RES:
		{
			float delta = ev.code == REL_HWHEEL_HI_RES ? ev.value / 120.0f : (float)ev.value;
			float wx = delta;
			float wy = 0.0f;

			BMessage* event = new BMessage(B_MOUSE_WHEEL_CHANGED);
			event->AddFloat("be:wheel_delta_x", wx);
			event->AddFloat("be:wheel_delta_y", wy);
			event->AddInt64("when", system_time());

			BAutolock lock(fEventListLocker);
			fEventList.AddItem(event);
			if (fWaitingOnEvent) {
				fWaitingOnEvent = false;
				lock.Unlock();
				release_sem(fEventNotification);
			}
			break;
		}
	}
}


void
LibEvdevEventStream::_ProcessAbsEvent(EvdevDevice& dev, struct input_event& ev)
{
	// Handle absolute positioning (touchpad in absolute mode, touch screens).
	// ABS_X/ABS_Y arrive as separate events, so each rotates the pair using
	// the other axis' last known normalized value.
	switch (ev.code) {
		case ABS_X:
		{
			int min = libevdev_get_abs_minimum(dev.evdev, ABS_X);
			int max = libevdev_get_abs_maximum(dev.evdev, ABS_X);
			if (max > min) {
				fLastAbsU = (float)(ev.value - min) / (max - min);
				float outU, outV;
				_RotatePoint(fLastAbsU, fLastAbsV, outU, outV);
				fMousePosition.x = outU * fWidth;
				fMousePosition.y = outV * fHeight;
				fMouseMoved = true;
			}
			break;
		}

		case ABS_Y:
		{
			int min = libevdev_get_abs_minimum(dev.evdev, ABS_Y);
			int max = libevdev_get_abs_maximum(dev.evdev, ABS_Y);
			if (max > min) {
				fLastAbsV = (float)(ev.value - min) / (max - min);
				float outU, outV;
				_RotatePoint(fLastAbsU, fLastAbsV, outU, outV);
				fMousePosition.x = outU * fWidth;
				fMousePosition.y = outV * fHeight;
				fMouseMoved = true;
			}
			break;
		}
	}
}


void
LibEvdevEventStream::_ProcessButtonEvent(EvdevDevice& dev, struct input_event& ev)
{
	uint32 bButton = MapMouseButton(ev.code);
	if (bButton == 0)
		return;

	bool pressed = (ev.value != 0);
	uint32 oldButtons = fMouseButtons;

	if (pressed)
		fMouseButtons |= bButton;
	else
		fMouseButtons &= ~bButton;

	if (fMouseButtons != oldButtons) {
		_SendMouseEvent(pressed ? B_MOUSE_DOWN : B_MOUSE_UP);
	}
}


void
LibEvdevEventStream::_FlushPendingEvents()
{
	if (fMouseMoved) {
		// Apply pending delta for relative motion
		if (fPendingMouseDelta.x != 0 || fPendingMouseDelta.y != 0) {
			// Relative motion is never rotated; see MouseInputDevice.
			float dx = fPendingMouseDelta.x;
			float dy = fPendingMouseDelta.y;
			fMousePosition.x += dx;
			fMousePosition.y += dy;

			fMousePosition.x = clamp_constref(fMousePosition.x, 0.0f, (float)(fWidth - 1));
			fMousePosition.y = clamp_constref(fMousePosition.y, 0.0f, (float)(fHeight - 1));

			fPendingMouseDelta = BPoint(0, 0);
		}

		_SendMouseEvent(B_MOUSE_MOVED);
		fMouseMoved = false;
	}
}


void
LibEvdevEventStream::_SendKeyEvent(uint32 what, int32 key, int32 character,
	uint32 modifiers)
{
	BMessage* event = new BMessage(what);
	event->AddInt32("key", key);
	event->AddInt32("raw_char", character);
	event->AddInt32("modifiers", modifiers);

	char byte = (char)character;
	char bytes[2] = {byte, 0};
	event->AddInt8("byte", (int8)byte);
	if (character != 0)
		event->AddString("bytes", bytes);
	event->AddInt64("when", system_time());

	BAutolock lock(fEventListLocker);
	fEventList.AddItem(event);
	if (fWaitingOnEvent) {
		fWaitingOnEvent = false;
		lock.Unlock();
		release_sem(fEventNotification);
	}
}


void
LibEvdevEventStream::_SendMouseEvent(uint32 what)
{
	BMessage* event = new BMessage(what);
	event->AddPoint("where", fMousePosition);
	event->AddInt32("buttons", fMouseButtons);
	event->AddInt32("modifiers", fModifiers);
	event->AddInt64("when", system_time());

	if (what == B_MOUSE_DOWN)
		event->AddInt32("clicks", 1);

	BAutolock lock(fEventListLocker);
	fEventList.AddItem(event);

	if (what == B_MOUSE_MOVED)
		fLatestMouseMovedEvent = event;

	if (fWaitingOnEvent) {
		fWaitingOnEvent = false;
		lock.Unlock();
		release_sem(fEventNotification);
	}
}


int32
LibEvdevEventStream::_MapCharacter(uint32 linuxKeyCode, bool shift, bool caps)
{
	for (size_t i = 0; i < sizeof(kKeyMap) / sizeof(kKeyMap[0]); i++) {
		if (kKeyMap[i].linuxKey != linuxKeyCode)
			continue;

		uint32 plain = kKeyMap[i].Bkey;
		bool isLetter = plain >= 'a' && plain <= 'z';
		// Caps Lock: letters only, Shift: anything with shifted form
		bool upper = isLetter ? (shift != caps) : shift;
		if (upper && kKeyMap[i].shifted != 0)
			return kKeyMap[i].shifted;

		return plain;
	}

	return 0;
}


void
LibEvdevEventStream::_UpdateModifiers(uint32 keyCode, bool pressed)
{
	switch (keyCode) {
		case KEY_LEFTSHIFT:
			if (pressed) fModifiers |= B_LEFT_SHIFT_KEY | B_SHIFT_KEY;
			else fModifiers &= ~(B_LEFT_SHIFT_KEY | B_SHIFT_KEY);
			break;
		case KEY_RIGHTSHIFT:
			if (pressed) fModifiers |= B_RIGHT_SHIFT_KEY | B_SHIFT_KEY;
			else fModifiers &= ~(B_RIGHT_SHIFT_KEY | B_SHIFT_KEY);
			break;
		// ctrl mode (greeter runs before keymap loads): Ctrl=Command, Alt=Control, Super=Option
		case KEY_LEFTCTRL:
			if (pressed) fModifiers |= B_LEFT_COMMAND_KEY | B_COMMAND_KEY;
			else fModifiers &= ~(B_LEFT_COMMAND_KEY | B_COMMAND_KEY);
			break;
		case KEY_RIGHTCTRL:
			if (pressed) fModifiers |= B_RIGHT_COMMAND_KEY | B_COMMAND_KEY;
			else fModifiers &= ~(B_RIGHT_COMMAND_KEY | B_COMMAND_KEY);
			break;
		case KEY_LEFTALT:
			if (pressed) fModifiers |= B_LEFT_CONTROL_KEY | B_CONTROL_KEY;
			else fModifiers &= ~(B_LEFT_CONTROL_KEY | B_CONTROL_KEY);
			break;
		case KEY_RIGHTALT:
			if (pressed) fModifiers |= B_RIGHT_OPTION_KEY | B_OPTION_KEY;
			else fModifiers &= ~(B_RIGHT_OPTION_KEY | B_OPTION_KEY);
			break;
		case KEY_LEFTMETA:
			if (pressed) fModifiers |= B_LEFT_OPTION_KEY | B_OPTION_KEY;
			else fModifiers &= ~(B_LEFT_OPTION_KEY | B_OPTION_KEY);
			break;
		case KEY_RIGHTMETA:
			if (pressed) fModifiers |= B_RIGHT_OPTION_KEY | B_OPTION_KEY;
			else fModifiers &= ~(B_RIGHT_OPTION_KEY | B_OPTION_KEY);
			break;
		case KEY_CAPSLOCK:
			if (pressed) {
				fCapsLock = !fCapsLock;
				if (fCapsLock) fModifiers |= B_CAPS_LOCK;
				else fModifiers &= ~B_CAPS_LOCK;
			}
			break;
		case KEY_NUMLOCK:
			if (pressed) {
				fNumLock = !fNumLock;
				if (fNumLock) fModifiers |= B_NUM_LOCK;
				else fModifiers &= ~B_NUM_LOCK;
			}
			break;
		case KEY_SCROLLLOCK:
			if (pressed) {
				fScrollLock = !fScrollLock;
				if (fScrollLock) fModifiers |= B_SCROLL_LOCK;
				else fModifiers &= ~B_SCROLL_LOCK;
			}
			break;
	}
}


uint32
LibEvdevEventStream::_GetCurrentModifiers() const
{
	return fModifiers;
}
