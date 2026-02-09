/*
 * Copyright 2019-2026, Dario Casalinuovo
 * Distributed under the terms of the GPL License.
 */

#include "LibInputEventStream.h"

#include <Autolock.h>
#include <InterfaceDefs.h>
#include <Screen.h>
#include <View.h>

#include <sys/epoll.h>
#include <time.h>


static const struct {
	uint32 linuxKey;
	uint32 Bkey;
} kKeyMap[] = {
	// Control / modifiers
	{ KEY_ESC,        B_ESCAPE },
	{ KEY_TAB,        B_TAB },
	{ KEY_CAPSLOCK,   B_CAPS_LOCK },
	{ KEY_LEFTSHIFT,  B_SHIFT_KEY },
	{ KEY_RIGHTSHIFT, B_SHIFT_KEY },
	{ KEY_LEFTCTRL,   B_CONTROL_KEY },
	{ KEY_RIGHTCTRL,  B_CONTROL_KEY },
	{ KEY_LEFTALT,    B_LEFT_OPTION_KEY },
	{ KEY_RIGHTALT,   B_RIGHT_OPTION_KEY },
	{ KEY_LEFTMETA,   B_LEFT_COMMAND_KEY },
	{ KEY_RIGHTMETA,  B_RIGHT_COMMAND_KEY },
	{ KEY_LEFT,       B_LEFT_ARROW },
	{ KEY_RIGHT,      B_RIGHT_ARROW },
	{ KEY_UP,         B_UP_ARROW },
	{ KEY_DOWN,       B_DOWN_ARROW },

	// Top row / numbers
	{ KEY_1,          '1' },
	{ KEY_2,          '2' },
	{ KEY_3,          '3' },
	{ KEY_4,          '4' },
	{ KEY_5,          '5' },
	{ KEY_6,          '6' },
	{ KEY_7,          '7' },
	{ KEY_8,          '8' },
	{ KEY_9,          '9' },
	{ KEY_0,          '0' },
	{ KEY_MINUS,      '-' },
	{ KEY_EQUAL,      '=' },
	{ KEY_BACKSPACE,  B_BACKSPACE },
	{ KEY_ENTER,      B_ENTER },

	// QWERTY
	{ KEY_Q, 'q' }, { KEY_W, 'w' }, { KEY_E, 'e' }, { KEY_R, 'r' },
	{ KEY_T, 't' }, { KEY_Y, 'y' }, { KEY_U, 'u' }, { KEY_I, 'i' },
	{ KEY_O, 'o' }, { KEY_P, 'p' }, { KEY_LEFTBRACE, '[' }, { KEY_RIGHTBRACE, ']' },
	{ KEY_BACKSLASH, '\\' },

	{ KEY_A, 'a' }, { KEY_S, 's' }, { KEY_D, 'd' }, { KEY_F, 'f' },
	{ KEY_G, 'g' }, { KEY_H, 'h' }, { KEY_J, 'j' }, { KEY_K, 'k' },
	{ KEY_L, 'l' }, { KEY_SEMICOLON, ';' }, { KEY_APOSTROPHE, '\'' },

	{ KEY_Z, 'z' }, { KEY_X, 'x' }, { KEY_C, 'c' }, { KEY_V, 'v' },
	{ KEY_B, 'b' }, { KEY_N, 'n' }, { KEY_M, 'm' }, { KEY_COMMA, ',' },
	{ KEY_DOT, '.' }, { KEY_SLASH, '/' }, { KEY_GRAVE, '`' },

	{ KEY_SPACE,     B_SPACE },

	// Function keys
	{ KEY_F1,  B_F1_KEY },  { KEY_F2,  B_F2_KEY },  { KEY_F3,  B_F3_KEY },
	{ KEY_F4,  B_F4_KEY },  { KEY_F5,  B_F5_KEY },  { KEY_F6,  B_F6_KEY },
	{ KEY_F7,  B_F7_KEY },  { KEY_F8,  B_F8_KEY },  { KEY_F9,  B_F9_KEY },
	{ KEY_F10, B_F10_KEY }, { KEY_F11, B_F11_KEY }, { KEY_F12, B_F12_KEY },

	// Navigation / editing
	{ KEY_HOME,     B_HOME },
	{ KEY_END,      B_END },
	{ KEY_PAGEUP,   B_PAGE_UP },
	{ KEY_PAGEDOWN, B_PAGE_DOWN },
	{ KEY_INSERT,   B_INSERT },
	{ KEY_DELETE,   B_DELETE },
	{ KEY_SCROLLLOCK, B_SCROLL_LOCK },

	// Keypad
	{ KEY_KP0,        '0' },
	{ KEY_KP1,        '1' },
	{ KEY_KP2,        '2' },
	{ KEY_KP3,        '3' },
	{ KEY_KP4,        '4' },
	{ KEY_KP5,        '5' },
	{ KEY_KP6,        '6' },
	{ KEY_KP7,        '7' },
	{ KEY_KP8,        '8' },
	{ KEY_KP9,        '9' },
	{ KEY_KPDOT,      '.' },
	{ KEY_KPMINUS,    '-' },
	{ KEY_KPPLUS,     '+' },
	{ KEY_KPASTERISK, '*' },
	{ KEY_KPSLASH,    '/' },
	{ KEY_KPENTER,    B_ENTER },

	// International / extras
	{ KEY_102ND,      '\\' },

	// Sentinel
	{ 0, 0 }
};


static uint32 MapMouseButton(uint32 linuxButton)
{
	switch (linuxButton) {
		case BTN_LEFT:
			return B_PRIMARY_MOUSE_BUTTON;
		case BTN_RIGHT:
			return B_SECONDARY_MOUSE_BUTTON;
		case BTN_MIDDLE:
			return B_TERTIARY_MOUSE_BUTTON;
		case BTN_SIDE:
			return 0x08;
		case BTN_EXTRA:
			return 0x10;
		default:
			return 0;
	}
}


static int GetVTSwitchNumber(uint32 keyCode, uint32 modifiers)
{
	bool hasAlt = (modifiers & B_OPTION_KEY) != 0;
	bool hasShift = (modifiers & B_SHIFT_KEY) != 0;

	if (hasAlt && !hasShift) {
		if (keyCode >= KEY_F1 && keyCode <= KEY_F10)
			return (keyCode - KEY_F1) + 1;
		if (keyCode == KEY_F11)
			return 11;
		if (keyCode == KEY_F12)
			return 12;
	}
	return 0;
}


int
LibInputEventStream::_OpenRestricted(const char* path, int flags, void* userData)
{
	LibInputEventStream* stream = static_cast<LibInputEventStream*>(userData);

	if (stream->fSeat != NULL) {
		int fd = -1;
		int deviceId = libseat_open_device(stream->fSeat, path, &fd);
	
		if (deviceId >= 0 && fd >= 0) {
			stream->fDeviceIds[fd] = deviceId;
			return fd;
		}
	
		return -errno;
	}

	int fd = open(path, flags);

	return fd < 0 ? -errno : fd;
}


void
LibInputEventStream::_CloseRestricted(int fd, void* userData)
{
	LibInputEventStream* stream = static_cast<LibInputEventStream*>(userData);

	if (stream->fSeat != NULL) {
		auto it = stream->fDeviceIds.find(fd);
		if (it != stream->fDeviceIds.end()) {
		
			libseat_close_device(stream->fSeat, it->second);
			stream->fDeviceIds.erase(it);
			return;
		}
	}

	close(fd);
}


static const struct libinput_interface sLibInputInterface = {
	.open_restricted = LibInputEventStream::_OpenRestricted,
	.close_restricted = LibInputEventStream::_CloseRestricted,
};


LibInputEventStream::LibInputEventStream(uint32 width, uint32 height, struct libseat* seat)
	:
	fEventList(10, true),
	fEventListLocker("libinput event list"),
	fEventNotification(-1),
	fWaitingOnEvent(false),
	fLatestMouseMovedEvent(NULL),
	fMousePosition(0, 0),
	fMouseButtons(0),
	fModifiers(0),
	fOldModifiers(0),
	fCapsLock(false),
	fNumLock(false),
	fScrollLock(false),
	fRunning(true),
	fSuspended(false),
	fUDevHandle(NULL),
	fInputHandle(NULL),
	fWidth(width),
	fHeight(height),
	fSeat(seat),
	fDeviceIdsLock("libinput device ids"),
	fLibInputLock("libinput api lock"),
	fLastResumeTime(0),
	fSeatMutex(NULL)
{
	memset(fKeyStates, 0, sizeof(fKeyStates));

	fEventNotification = create_sem(0, "libinput event notification");

	fUDevHandle = udev_new();
	if (!fUDevHandle)	
		return;

	fInputHandle = libinput_udev_create_context(&sLibInputInterface, this, fUDevHandle);
	if (!fInputHandle) {
		udev_unref(fUDevHandle);
		fUDevHandle = NULL;
		return;
	}

	if (libinput_udev_assign_seat(fInputHandle, "seat0") != 0) {
		libinput_unref(fInputHandle);
		fInputHandle = NULL;
		udev_unref(fUDevHandle);
		fUDevHandle = NULL;
		return;
	}

	thread_id pollThread = spawn_thread(_PollEventsThread, "libinput events",
		B_NORMAL_PRIORITY, (void*)this);
	resume_thread(pollThread);


}


LibInputEventStream::~LibInputEventStream()
{
	fRunning = false;

	if (fSuspended)
		Resume();

	delete_sem(fEventNotification);

	if (fInputHandle)
		libinput_unref(fInputHandle);
	if (fUDevHandle)
		udev_unref(fUDevHandle);
}


void
LibInputEventStream::UpdateScreenBounds(BRect bounds)
{
	fWidth = bounds.IntegerWidth() + 1;
	fHeight = bounds.IntegerHeight() + 1;
}


bool
LibInputEventStream::GetNextEvent(BMessage** _event)
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
LibInputEventStream::InsertEvent(BMessage* event)
{
	BAutolock lock(fEventListLocker);
	if (!lock.IsLocked() || !fEventList.AddItem(event))
		return B_ERROR;

	if (event->what == B_MOUSE_MOVED)
		fLatestMouseMovedEvent = event;

	return B_OK;
}


BMessage*
LibInputEventStream::PeekLatestMouseMoved()
{
	return fLatestMouseMovedEvent;
}


void
LibInputEventStream::Suspend()
{


	BAutolock lock(fLibInputLock);
	if (!lock.IsLocked())
		return;

	if (fSuspended)	
		return;

	libinput_suspend(fInputHandle);

	libinput_event* inputEvent;
	int drained = 0;
	while ((inputEvent = libinput_get_event(fInputHandle)) != NULL) {
		libinput_event_destroy(inputEvent);
		drained++;
	}

	fSuspended = true;
}


void
LibInputEventStream::Resume()
{


	BAutolock lock(fLibInputLock);
	if (!lock.IsLocked())
		return;

	if (!fSuspended)
		return;

	int ret = libinput_resume(fInputHandle);
	if (ret != 0)
		printf("libinput: resume fail\n");

	fSuspended = false;
	fLastResumeTime = system_time();
}


void
LibInputEventStream::_PollEventsThread(void* cookie)
{
	LibInputEventStream* owner = (LibInputEventStream*)cookie;
	owner->_PollEvents();
}


void
LibInputEventStream::_PollEvents()
{
	int epollFd = epoll_create1(0);
	if (epollFd < 0)
		return;

	epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = libinput_get_fd(fInputHandle);

	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
		close(epollFd);
		return;
	}

	while (fRunning) {
		epoll_wait(epollFd, &event, 1, 200);

		if (fSuspended)
			continue;

		BAutolock lock(fLibInputLock);
		if (!lock.IsLocked() || fSuspended)
			continue;

		if (libinput_dispatch(fInputHandle) < 0) {
			close(epollFd);
			return;
		}

		libinput_event* inputEvent;
		while ((inputEvent = libinput_get_event(fInputHandle)) != NULL) {
			_ScheduleEvent(inputEvent);
			libinput_event_destroy(inputEvent);
		}
	}

	close(epollFd);
}


void
LibInputEventStream::_ScheduleEvent(libinput_event* ev)
{
	libinput_event_type type = libinput_event_get_type(ev);
	uint32 what = 0;

	BMessage* event = new BMessage();
	if (event == NULL)
		return;

	switch (type) {
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			delete event;
			return;

		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		{
			libinput_event_pointer* e = libinput_event_get_pointer_event(ev);

			what = B_MOUSE_MOVED;

			double dx = libinput_event_pointer_get_absolute_x_transformed(e, fWidth);
			double dy = libinput_event_pointer_get_absolute_y_transformed(e, fHeight);
			fMousePosition.x = static_cast<int>(dx);
			fMousePosition.y = static_cast<int>(dy);
			event->AddPoint("where", fMousePosition);

			fLatestMouseMovedEvent = event;
			break;
		}

		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		{
			libinput_event_pointer* e = libinput_event_get_pointer_event(ev);

			if (type == LIBINPUT_EVENT_POINTER_MOTION)
				what = B_MOUSE_MOVED;
			else if (libinput_event_pointer_get_button_state(e) == LIBINPUT_BUTTON_STATE_PRESSED)
				what = B_MOUSE_DOWN;
			else
				what = B_MOUSE_UP;

			fMouseButtons = libinput_event_pointer_get_button(e);

			double dx = libinput_event_pointer_get_dx(e);
			double dy = libinput_event_pointer_get_dy(e);

			fMousePosition.x += static_cast<int>(dx);
			fMousePosition.y += static_cast<int>(dy);

			fMousePosition.x = std::min((float)fWidth, fMousePosition.x);
			fMousePosition.x = std::max(0.0f, fMousePosition.x);

			fMousePosition.y = std::min((float)fHeight, fMousePosition.y);
			fMousePosition.y = std::max(0.0f, fMousePosition.y);

			uint32 bButton = MapMouseButton(fMouseButtons);

			event->AddPoint("where", fMousePosition);
			if (what == B_MOUSE_MOVED || what == B_MOUSE_DOWN)
				event->AddInt32("buttons", bButton);

			fLatestMouseMovedEvent = event;
			break;
		}

		case LIBINPUT_EVENT_POINTER_AXIS:
		{
			libinput_event_pointer* e = libinput_event_get_pointer_event(ev);

			double value = 0;
			if (libinput_event_pointer_has_axis(e, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
				value = libinput_event_pointer_get_axis_value(e, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
				event->AddFloat("be:wheel_delta_y", value);
			}
			if (libinput_event_pointer_has_axis(e, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
				value = libinput_event_pointer_get_axis_value(e, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
				event->AddFloat("be:wheel_delta_x", value);
			}

			what = B_MOUSE_WHEEL_CHANGED;
			break;
		}

		case LIBINPUT_EVENT_KEYBOARD_KEY:
		{
			libinput_event_keyboard* e = libinput_event_get_keyboard_event(ev);
			uint32 keyCode = libinput_event_keyboard_get_key(e);
			libinput_key_state state = libinput_event_keyboard_get_key_state(e);
			bool pressed = (state == LIBINPUT_KEY_STATE_PRESSED);

			_UpdateModifiers(keyCode, pressed);
			uint32 currentModifiers = _GetCurrentModifiers();

			if (pressed && fSeat != NULL) {
				int vtNum = GetVTSwitchNumber(keyCode, currentModifiers);
				if (vtNum > 0) {
					bigtime_t now = system_time();
					bigtime_t elapsed = now - fLastResumeTime;
					if (fLastResumeTime > 0 && elapsed < kVTSwitchCooldown) {
						delete event;
						return;
					}

					int ret;
					if (fSeatMutex) {
						std::lock_guard<std::mutex> seatLock(*fSeatMutex);
						ret = libseat_switch_session(fSeat, vtNum);
					} else {
						ret = libseat_switch_session(fSeat, vtNum);
					}

					if (ret < 0) {
						printf("libseat_switch_session FAILED: %d (errno=%d: %s)\n",
							ret, errno, strerror(errno));
					}
					delete event;
					return;
				}
			}

			int32 Bkey = _MapKeyCode(keyCode);
			if (Bkey == 0) {
				delete event;
				return;
			}

			if (pressed) {
				int32 repeatCount = fKeyStates[keyCode] ? 2 : 1;
				fKeyStates[keyCode] = true;
				what = B_KEY_DOWN;
				_AddKeyEvent(event, B_KEY_DOWN, Bkey, currentModifiers, repeatCount);
			} else {
				fKeyStates[keyCode] = false;
				what = B_KEY_UP;
				_AddKeyEvent(event, B_KEY_UP, Bkey, currentModifiers, 1);
			}

			if (currentModifiers != fOldModifiers) {
				event->AddInt32("be:old_modifiers", fOldModifiers);
				event->AddInt32("modifiers", currentModifiers);
				what = B_MODIFIERS_CHANGED;
				fOldModifiers = currentModifiers;
			}
			break;
		}

		case LIBINPUT_EVENT_TOUCH_DOWN:
		case LIBINPUT_EVENT_TOUCH_MOTION:
		case LIBINPUT_EVENT_TOUCH_UP:
		case LIBINPUT_EVENT_TOUCH_CANCEL:
		case LIBINPUT_EVENT_TOUCH_FRAME:
		default:
			delete event;
			return;
	}

	if (what == 0) {
		delete event;
		return;
	}

	event->what = what;
	event->AddInt64("when", system_time());

	BAutolock lock(fEventListLocker);
	fEventList.AddItem(event);
	if (fWaitingOnEvent) {
		fWaitingOnEvent = false;
		lock.Unlock();
		release_sem(fEventNotification);
	}
}


int32
LibInputEventStream::_MapKeyCode(uint32 linuxKeyCode)
{
	for (size_t i = 0; i < sizeof(kKeyMap) / sizeof(kKeyMap[0]); i++) {
		if (kKeyMap[i].linuxKey == linuxKeyCode)
			return kKeyMap[i].Bkey;
	}
	return linuxKeyCode;
}


void
LibInputEventStream::_UpdateModifiers(uint32 keyCode, bool pressed)
{
	switch (keyCode) {
		case KEY_LEFTSHIFT:
			if (pressed) fModifiers |= B_SHIFT_KEY;
			else fModifiers &= ~B_SHIFT_KEY;
			break;
		case KEY_RIGHTSHIFT:
			if (pressed) fModifiers |= B_RIGHT_SHIFT_KEY;
			else fModifiers &= ~B_RIGHT_SHIFT_KEY;
			break;

		case KEY_LEFTCTRL:
		case KEY_RIGHTCTRL:
			if (pressed) fModifiers |= B_CONTROL_KEY;
			else fModifiers &= ~B_CONTROL_KEY;
			break;

		case KEY_LEFTALT:
		case KEY_RIGHTALT:
			if (pressed) fModifiers |= B_OPTION_KEY;
			else fModifiers &= ~B_OPTION_KEY;
			break;

		case KEY_LEFTMETA:
		case KEY_RIGHTMETA:
			if (pressed) fModifiers |= B_COMMAND_KEY;
			else fModifiers &= ~B_COMMAND_KEY;
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
LibInputEventStream::_GetCurrentModifiers() const
{
	return fModifiers;
}


void
LibInputEventStream::_AddKeyEvent(BMessage* message, uint32 what, int32 key,
	uint32 modifiers, int32 repeatCount)
{
	message->AddInt32("key", key);
	message->AddInt32("raw_char", key);
	message->AddInt32("modifiers", modifiers);

	char byte = (char)key;
	char bytes[2] = {byte, 0};
	message->AddInt8("byte", (int8)byte);
	message->AddString("bytes", bytes);

	if (what == B_KEY_DOWN && repeatCount > 1)
		message->AddInt32("be:key_repeat", repeatCount);
}
