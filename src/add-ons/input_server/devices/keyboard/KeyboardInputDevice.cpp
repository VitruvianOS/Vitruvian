/*
 * Copyright 2004-2006, Jérôme Duval. All rights reserved.
 * Copyright 2005-2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2008-2009, Stephan Aßmus, superstippi@gmx.de.
 * Copyright 2026, Dario Casalinuovo, superstippi@gmx.de.
 *
 * Distributed under the terms of the GPL License.
 */


#include "KeyboardInputDevice.h"

#include <errno.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <Application.h>
#include <AutoDeleter.h>
#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <keyboard_mouse_driver.h>

#include <sys/epoll.h>
#include <linux/vt.h>
#include "../LinuxEvdevShim.h"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <AppDefs.h>
#include <private/app/RegistrarDefs.h>



#undef TRACE

//#define TRACE_KEYBOARD_DEVICE
#ifdef TRACE_KEYBOARD_DEVICE

#include <private/shared/FunctionTracer.h>

static	int32		sFunctionDepth = -1;

#	define CALLED(x...) \
		FunctionTracer _ft(debug_printf, this, __PRETTY_FUNCTION__, sFunctionDepth)
#	define TRACE(x...) \
		do { BString _to; \
			_to.Append(' ', (sFunctionDepth + 1) * 2); \
			debug_printf("%p -> %s", this, _to.String()); \
			debug_printf(x); } while (0)
#	define LOG_EVENT(text...) debug_printf(text)
#	define LOG_ERR(text...) TRACE(text)
#else
#	define TRACE(x...) do {} while (0)
#	define CALLED(x...) TRACE(x)
#	define LOG_ERR(text...) debug_printf(text)
#	define LOG_EVENT(text...) TRACE(x)
#endif


const static uint32 kKeyboardThreadPriority = B_FIRST_REAL_TIME_PRIORITY + 4;
const static char* kKeyboardDevicesDirectory = "/dev/input";


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) KeyboardInputDevice();
}


static char*
get_short_name(const char* longName)
{
	BString string(longName);
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(name, slash + 1, string.Length() - slash);
	int32 index = atoi(name.String()) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	// some special handling so that we get "USB" and "AT" instead of "usb"/"at"
	if (name.Length() < 4)
		name.ToUpper();
	else
		name.Capitalize();

	name << " Keyboard " << index;

	return strdup(name.String());
}


//	#pragma mark -


KeyboardDevice::KeyboardDevice(KeyboardInputDevice* owner, const char* path)
	:
	BHandler("keyboard device"),
	fOwner(owner),
	fFD(-1),
	fInputHandle(NULL),
	fEpollFd(-1),
	fXkbContext(NULL),
	fXkbKeymap(NULL),
	fXkbState(NULL),
	fXkbComposeTable(NULL),
	fXkbComposeState(NULL),
	fThread(-1),
	fActive(false),
	fInputMethodStarted(false),
	fKeyboardID(0),
	fUpdateSettings(false),
	fSettingsCommand(0),
	fKeymapLock("keymap lock")
{
	CALLED();

	strlcpy(fPath, path, B_PATH_NAME_LENGTH);
	fDeviceRef.name = get_short_name(path);
	fDeviceRef.type = B_KEYBOARD_DEVICE;
	fDeviceRef.cookie = this;

	if (be_app->Lock()) {
		be_app->AddHandler(this);
		be_app->Unlock();
	}
}


KeyboardDevice::~KeyboardDevice()
{
	CALLED();
	TRACE("delete\n");

	if (fActive)
		Stop();

	free(fDeviceRef.name);

	if (fXkbComposeState != NULL) {
		xkb_compose_state_unref(fXkbComposeState);
		fXkbComposeState = NULL;
	}

	if (be_app->Lock()) {
		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


void
KeyboardDevice::MessageReceived(BMessage* message)
{
	CALLED();

	switch (message->what) {
		case B_SEAT_ENABLED:
		{
			uint8 leds[LED_MAX / 8 + 1] = {};
			if (ioctl(fFD, EVIOCGLED(sizeof(leds)), leds) >= 0) {
				static const struct { uint32_t ledBit; xkb_keycode_t xkbCode; } kLeds[] = {
					{ LED_CAPSL,   58 + 8 },
					{ LED_NUML,    69 + 8 },
					{ LED_SCROLLL, 70 + 8 },
				};
				for (auto& led : kLeds) {
					if (leds[led.ledBit / 8] & (1 << (led.ledBit % 8))) {
						xkb_state_update_key(fXkbState, led.xkbCode, XKB_KEY_DOWN);
						xkb_state_update_key(fXkbState, led.xkbCode, XKB_KEY_UP);
					}
				}
			}
			break;
		}

		case B_INPUT_METHOD_EVENT:
		{
			int32 opcode;
			if (message->FindInt32("be:opcode", &opcode) != B_OK)
				return;
			if (opcode == B_INPUT_METHOD_STOPPED)
				fInputMethodStarted = false;
			break;
		}

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


status_t
KeyboardDevice::Start()
{
	CALLED();
	TRACE("name: %s\n", fDeviceRef.name);

	fFD = open(fPath, O_RDWR | O_NONBLOCK);
	if (fFD >= 0) {
		if (libevdev_new_from_fd(fFD, &fInputHandle) < 0) {
			close(fFD);
			fFD = -1;
		} else {
			// No EVIOCGRAB: input_server add-ons are the sole input path;
			// libevdev reads directly from /dev/input/eventN.
			_RebuildXkb();
		}
	}
	if (fFD < 0) {
		// let the control thread handle any error on opening the device
		fFD = errno > 0 ? -errno : -1;
	}

	char threadName[B_OS_NAME_LENGTH];
	snprintf(threadName, B_OS_NAME_LENGTH, "%s watcher", fDeviceRef.name);

	fThread = spawn_thread(_ControlThreadEntry, threadName,
		kKeyboardThreadPriority, this);
	if (fThread < B_OK)
		return fThread;

	fActive = true;
	resume_thread(fThread);

	return fFD >= 0 ? B_OK : B_ERROR;
}


void
KeyboardDevice::Stop()
{
	CALLED();
	TRACE("name: %s\n", fDeviceRef.name);

	fActive = false;

	if (fEpollFd >= 0) {
		close(fEpollFd);
		fEpollFd = -1;
	}
	if (fXkbState != NULL) {
		xkb_state_unref(fXkbState);
		fXkbState = NULL;
	}
	if (fXkbKeymap != NULL) {
		xkb_keymap_unref(fXkbKeymap);
		fXkbKeymap = NULL;
	}
	if (fXkbContext != NULL) {
		xkb_context_unref(fXkbContext);
		fXkbContext = NULL;
	}
	if (fInputHandle != NULL) {
		libevdev_free(fInputHandle);
		fInputHandle = NULL;
	}
	if (fFD >= 0) {
		close(fFD);
		fFD = -1;
	}

	if (fThread >= 0) {
		suspend_thread(fThread);
		resume_thread(fThread);
		status_t dummy;
		wait_for_thread(fThread, &dummy);
	}
}


status_t
KeyboardDevice::UpdateSettings(uint32 opcode)
{
	CALLED();

	if (fThread < 0)
		return B_ERROR;

	// schedule updating the settings from within the control thread
	fSettingsCommand = opcode;
	fUpdateSettings = true;

	return B_OK;
}


// #pragma mark - control thread


// ---------------------------------------------------------------------------
// evdev keycode → Haiku special character table
// For keys that xkb_state_key_get_utf8 returns nothing for (arrow keys,
// navigation keys, function keys, system keys) but Haiku apps expect a
// specific byte value in the B_KEY_DOWN message's "bytes" field.
// haikuByte1 != 0 means two bytes are produced (used for F-keys:
//   bytes[0] = B_FUNCTION_KEY, bytes[1] = B_FN_KEY constant).
// ---------------------------------------------------------------------------

struct EvdevHaikuChar { uint32 code; uint8 byte0; uint8 byte1; };

static const EvdevHaikuChar kSpecialKeys[] = {
	// Control characters — xkb_state_key_get_utf8 skips the 0x01–0x1f range
	// on some xkbcommon versions/configurations, so we provide them here as a
	// guaranteed fallback.  Without these, Enter/Tab/Backspace/Escape arrive as
	// B_UNMAPPED_KEY_DOWN and are silently ignored by BTextView and friends.
	{ KEY_ESC,        B_ESCAPE,      0 },
	{ KEY_BACKSPACE,  B_BACKSPACE,   0 },
	{ KEY_TAB,        B_TAB,         0 },
	{ KEY_ENTER,      B_RETURN,      0 },
	{ KEY_KPENTER,    B_RETURN,      0 },
	// Cursor keys
	{ KEY_UP,        B_UP_ARROW,    0 },
	{ KEY_DOWN,      B_DOWN_ARROW,  0 },
	{ KEY_LEFT,      B_LEFT_ARROW,  0 },
	{ KEY_RIGHT,     B_RIGHT_ARROW, 0 },
	// Navigation cluster
	{ KEY_HOME,      B_HOME,        0 },
	{ KEY_END,       B_END,         0 },
	{ KEY_INSERT,    B_INSERT,      0 },
	{ KEY_DELETE,    B_DELETE,      0 },
	{ KEY_PAGEUP,    B_PAGE_UP,     0 },
	{ KEY_PAGEDOWN,  B_PAGE_DOWN,   0 },
	// Function keys: byte0 = B_FUNCTION_KEY, byte1 = B_Fn_KEY
	{ KEY_F1,  B_FUNCTION_KEY, B_F1_KEY  },
	{ KEY_F2,  B_FUNCTION_KEY, B_F2_KEY  },
	{ KEY_F3,  B_FUNCTION_KEY, B_F3_KEY  },
	{ KEY_F4,  B_FUNCTION_KEY, B_F4_KEY  },
	{ KEY_F5,  B_FUNCTION_KEY, B_F5_KEY  },
	{ KEY_F6,  B_FUNCTION_KEY, B_F6_KEY  },
	{ KEY_F7,  B_FUNCTION_KEY, B_F7_KEY  },
	{ KEY_F8,  B_FUNCTION_KEY, B_F8_KEY  },
	{ KEY_F9,  B_FUNCTION_KEY, B_F9_KEY  },
	{ KEY_F10, B_FUNCTION_KEY, B_F10_KEY },
	{ KEY_F11, B_FUNCTION_KEY, B_F11_KEY },
	{ KEY_F12, B_FUNCTION_KEY, B_F12_KEY },
	{ KEY_F13, B_FUNCTION_KEY, B_F13_KEY },
	{ KEY_F14, B_FUNCTION_KEY, B_F14_KEY },
	{ KEY_F15, B_FUNCTION_KEY, B_F15_KEY },
	{ KEY_F16, B_FUNCTION_KEY, B_F16_KEY },
	{ KEY_F17, B_FUNCTION_KEY, B_F17_KEY },
	{ KEY_F18, B_FUNCTION_KEY, B_F18_KEY },
	{ KEY_F19, B_FUNCTION_KEY, B_F19_KEY },
	{ KEY_F20, B_FUNCTION_KEY, B_F20_KEY },
	{ KEY_F21, B_FUNCTION_KEY, B_F21_KEY },
	{ KEY_F22, B_FUNCTION_KEY, B_F22_KEY },
	{ KEY_F23, B_FUNCTION_KEY, B_F23_KEY },
	{ KEY_F24, B_FUNCTION_KEY, B_F24_KEY },
	// System keys — same two-byte format as F-keys
	{ KEY_SYSRQ,      B_FUNCTION_KEY, B_PRINT_KEY  },
	{ KEY_SCROLLLOCK, B_FUNCTION_KEY, B_SCROLL_KEY },
	{ KEY_PAUSE,      B_FUNCTION_KEY, B_PAUSE_KEY  },
	{ 0, 0, 0 }
};


/*static*/ int32
KeyboardDevice::_ControlThreadEntry(void* arg)
{
	KeyboardDevice* device = (KeyboardDevice*)arg;
	return device->_ControlThread();
}


int32
KeyboardDevice::_ControlThread()
{
	CALLED();
	TRACE("fPath: %s\n", fPath);

	if (fFD < B_OK) {
		LOG_ERR("KeyboardDevice: error when opening %s: %s\n",
			fPath, strerror(fFD));
		_ControlThreadCleanup();
		// TOAST!
		return B_ERROR;
	}

	if (fXkbState == NULL)
		return B_ERROR;

	_UpdateSettings(0);

	fEpollFd = epoll_create1(0);
	if (fEpollFd < 0) {
		_ControlThreadCleanup();
		return B_ERROR;
	}
	struct epoll_event epev;
	epev.events = EPOLLIN;
	epev.data.fd = fFD;
	epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fFD, &epev);

	// Phase 2: keyboard ID via EVIOCGID
	if (fKeyboardID == 0) {
		struct input_id evid;
		if (ioctl(fFD, EVIOCGID, &evid) == 0) {
			fKeyboardID = (uint16)evid.product;
			BMessage message(IS_SET_KEYBOARD_ID);
			message.AddInt16("id", fKeyboardID);
			be_app->PostMessage(&message);
		}
	}

	// Phase 3: sync xkb state from current LED state (CapsLock/NumLock/ScrollLock)
	{
		uint8 leds[LED_MAX / 8 + 1] = {};
		if (ioctl(fFD, EVIOCGLED(sizeof(leds)), leds) >= 0) {
			// LED_CAPSLOCK = 0x01, LED_NUML = 0x00, LED_SCROLLL = 0x02
			// xkb keycodes for lock keys looked up dynamically by name
			// Simulate press+release to set the lock state in xkb
			xkb_keycode_t capsCode   = xkb_keymap_key_by_name(fXkbKeymap, "CAPS");
			xkb_keycode_t numCode    = xkb_keymap_key_by_name(fXkbKeymap, "NMLK");
			xkb_keycode_t scrollCode = xkb_keymap_key_by_name(fXkbKeymap, "SCLK");
			
			auto syncLock = [&](uint32_t ledBit, xkb_keycode_t xkbKey) {
				if (leds[ledBit / 8] & (1 << (ledBit % 8))) {
					xkb_state_update_key(fXkbState, xkbKey, XKB_KEY_DOWN);
					xkb_state_update_key(fXkbState, xkbKey, XKB_KEY_UP);
				}
			};
			syncLock(LED_CAPSL, capsCode);
			syncLock(LED_NUML,  numCode);
			syncLock(LED_SCROLLL, scrollCode);
		}
	}

	// VT switching setup
	int consoleFd = open("/dev/tty0", O_RDWR);
	bool hasRealVT = false;
	if (consoleFd >= 0) {
		struct vt_stat vtState;
		hasRealVT = (ioctl(consoleFd, VT_GETSTATE, &vtState) == 0);
	}

	bool isVM = false;
	{
		FILE* f = fopen("/sys/class/dmi/id/sys_vendor", "r");
		if (f != NULL) {
			char vendor[64] = {0};
			fgets(vendor, sizeof(vendor), f);
			fclose(f);
			isVM = (strstr(vendor, "QEMU") != NULL
				|| strstr(vendor, "VirtualBox") != NULL
				|| strstr(vendor, "VMware") != NULL
				|| strstr(vendor, "Microsoft") != NULL);
		}
	}

	bool vtLCtrl = false, vtRCtrl = false;
	bool vtAlt = false, vtRalt = false;
	bool menuKeyDown = false;

	raw_key_info keyInfo;
	uint32 lastKeyCode = 0;
	uint32 repeatCount = 1;
	uint8 states[16];
	bool ctrlAltDelPressed = false;

	memset(states, 0, sizeof(states));

	while (fActive) {
		// Update the settings from this thread if necessary
		if (fUpdateSettings) {
			_UpdateSettings(fSettingsCommand);
			fUpdateSettings = false;
		}

		struct epoll_event fired;
		if (epoll_wait(fEpollFd, &fired, 1, 100) <= 0) {
			/* Timeout: reconcile shadow state with the kernel's actual key
			 * state via EVIOCGKEY. Recovers from any dropped EV_KEY UP event
			 * (QEMU input grabs, libevdev/libinput drops, focus changes). */
#ifndef EVIOCGKEY
#define EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, len)
#endif
			if (fFD < 0)
				continue;
			/* 128 bytes = 1024 bits — covers all standard keys (KEY_MAX ≤ 767). */
			uint8_t keyBits[128] = {};
			if (ioctl(fFD, EVIOCGKEY(128), keyBits) != 0)
				continue;

#define _HWBIT(c) ((keyBits[(c) >> 3] >> ((c) & 7)) & 1)
#define _SHBIT(c) (states[(c) >> 3] & (1 << (7 - ((c) & 7))))

			// Ctrl+Alt+Del recovery (preserved from previous behavior).
			if (ctrlAltDelPressed) {
				bool delHeld  = _HWBIT(111);
				bool ctrlHeld = _HWBIT(29) || _HWBIT(97);
				bool altHeld  = _HWBIT(56) || _HWBIT(100);
				if (!delHeld || !ctrlHeld || !altHeld) {
					if (fOwner->fTeamMonitorWindow != NULL) {
						BMessage message(kMsgCtrlAltDelPressed);
						message.AddBool("key down", false);
						fOwner->fTeamMonitorWindow->PostMessage(&message);
					}
					ctrlAltDelPressed = false;
				}
			}

			// Generalized reconciliation: any key the shadow thinks is down
			// but the hardware reports up gets a synthetic release.
			bool anyRepaired = false;
			for (uint32 k = 1; k < 128; k++) {
				if (!_SHBIT(k) || _HWBIT(k))
					continue;
				if (!anyRepaired) {
					fKeymapLock.Lock();
					anyRepaired = true;
				}
				states[k >> 3] &= ~(1 << (7 - (k & 7)));
				xkb_state_update_key(fXkbState, k + 8, XKB_KEY_UP);

				if (k == KEY_LEFTCTRL)       vtLCtrl = false;
				else if (k == KEY_RIGHTCTRL) vtRCtrl = false;
				else if (k == KEY_LEFTALT)   vtAlt = false;
				else if (k == KEY_RIGHTALT)  vtRalt = false;
				else if (k == KEY_Menu)      menuKeyDown = false;

				BMessage* upMsg = new(std::nothrow) BMessage(B_UNMAPPED_KEY_UP);
				if (upMsg != NULL) {
					upMsg->AddInt64("when", system_time());
					upMsg->AddInt32("key", k);
					upMsg->AddInt32("modifiers", fModifiers);
					upMsg->AddData("states", B_UINT8_TYPE, states, 16);
					if (fOwner->EnqueueMessage(upMsg) != B_OK)
						delete upMsg;
				}
			}

			if (anyRepaired) {
				uint32 oldModifiers = fModifiers;
				uint32 newModifiers = 0;
#define _KBIT(c) (states[(c) >> 3] & (1 << (7 - ((c) & 7))))
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_SHIFT,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_SHIFT_KEY;
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CTRL,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_CONTROL_KEY;
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_ALT,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_COMMAND_KEY;
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_LOGO,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_OPTION_KEY;
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CAPS,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_CAPS_LOCK;
				if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_NUM,
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_NUM_LOCK;
				if (xkb_state_mod_name_is_active(fXkbState, "Scroll_Lock",
						XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_SCROLL_LOCK;
				if (menuKeyDown) newModifiers |= B_MENU_KEY;
				if (_KBIT(42))  newModifiers |= B_LEFT_SHIFT_KEY;
				if (_KBIT(54))  newModifiers |= B_RIGHT_SHIFT_KEY;
				if (_KBIT(29))  newModifiers |= B_LEFT_CONTROL_KEY;
				if (_KBIT(97))  newModifiers |= B_RIGHT_CONTROL_KEY;
				if (_KBIT(56))  newModifiers |= B_LEFT_COMMAND_KEY;
				if (_KBIT(100) && (newModifiers & B_COMMAND_KEY))
					newModifiers |= B_RIGHT_COMMAND_KEY;
				if (_KBIT(125)) newModifiers |= B_LEFT_OPTION_KEY;
				if (_KBIT(126)) newModifiers |= B_RIGHT_OPTION_KEY;
#undef _KBIT
				fModifiers = newModifiers;
				if (fModifiers != oldModifiers) {
					BMessage* m = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
					if (m != NULL) {
						m->AddInt64("when", system_time());
						m->AddInt32("be:old_modifiers", oldModifiers);
						m->AddInt32("modifiers", fModifiers);
						m->AddData("states", B_UINT8_TYPE, states, 16);
						if (fOwner->EnqueueMessage(m) != B_OK)
							delete m;
					}
					if ((newModifiers ^ oldModifiers)
							& (B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK))
						_UpdateLEDs();
				}
				fKeymapLock.Unlock();
			}
#undef _HWBIT
#undef _SHBIT
			continue;
		}

		struct input_event ev;
		int rc = libevdev_next_event(fInputHandle,
			LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			/* SYN_DROPPED: kernel dropped events (ring buffer overflow).
			 * Drain the libevdev sync queue so it exits sync mode; without
			 * this, every subsequent NORMAL read also returns STATUS_SYNC
			 * and no key events are ever delivered again. */
			struct input_event syncEv;
			while (libevdev_next_event(fInputHandle,
					LIBEVDEV_READ_FLAG_SYNC, &syncEv)
					== LIBEVDEV_READ_STATUS_SYNC)
				;
			/* Fall through to the EVIOCGKEY timeout path to resync our
			 * ctrlAltDelPressed state — handled in the epoll_wait block. */
			continue;
		}
		if (rc == -EAGAIN)
			continue;
		if (rc < 0) {
			_ControlThreadCleanup();
			return 0;
		}
		if (ev.type != EV_KEY)
			continue;

		keyInfo.keycode    = ev.code;
		keyInfo.is_keydown = (ev.value == 1 || ev.value == 2);
		keyInfo.timestamp  = (bigtime_t)ev.time.tv_sec * 1000000LL
			+ ev.time.tv_usec;

		uint32 keycode = keyInfo.keycode;  // raw evdev code
		bool isKeyDown = keyInfo.is_keydown;

		// Track raw modifier state for VT switching and Menu key (independent of keymap)
		if (ev.code == KEY_LEFTCTRL)       vtLCtrl   = isKeyDown;
		else if (ev.code == KEY_RIGHTCTRL) vtRCtrl   = isKeyDown;
		else if (ev.code == KEY_LEFTALT)   vtAlt     = isKeyDown;
		else if (ev.code == KEY_RIGHTALT)  vtRalt    = isKeyDown;
		else if (ev.code == KEY_Menu)      menuKeyDown = isKeyDown;

		// VT switch: Ctrl+Alt+Fn (native) or Alt+Fn (VM, left alt only)
		if (hasRealVT && isKeyDown && consoleFd >= 0) {
			uint32 fn = 0;
			if (ev.code >= KEY_F1 && ev.code <= KEY_F10)
				fn = ev.code - KEY_F1 + 1;
			else if (ev.code == KEY_F11) fn = 11;
			else if (ev.code == KEY_F12) fn = 12;

			if (fn > 0) {
				bool vtCtrl = vtLCtrl || vtRCtrl;
				bool trigger = isVM
					? (vtAlt && !vtCtrl && !vtRalt)
					: (vtCtrl && vtAlt);
				if (trigger) {
					ioctl(consoleFd, VT_ACTIVATE, fn);
					continue;
				}
			}
		}

		// Power management keys — send shutdown/reboot to registrar on key release.
		if (!isKeyDown) {
			bool isShutdown = (ev.code == KEY_POWER);
			bool isReboot   = (ev.code == KEY_RESTART);
			if (isShutdown || isReboot) {
				port_id regPort = find_port(B_REGISTRAR_PORT_NAME);
				if (regPort >= 0) {
					BMessage msg(BPrivate::B_REG_SHUT_DOWN);
					msg.AddBool("reboot", isReboot);
					msg.AddBool("confirm", false);
					ssize_t size = msg.FlattenedSize();
					char* buf = new char[size];
					if (msg.Flatten(buf, size) == B_OK)
						write_port(regPort, 0, buf, size);
					delete[] buf;
				}
				continue;
			}
		}

		LOG_EVENT("KB_READ: %" B_PRIdBIGTIME ", %02x, %02" B_PRIx32 "\n",
			keyInfo.timestamp, isKeyDown, keycode);

		if (keycode == 0)
			continue;

		if (isKeyDown && keycode == 139 /* KEY_MENU */) {
			// MENU KEY for Tracker
			bool noOtherKeyPressed = true;
			for (int32 i = 0; i < 16; i++) {
				if (states[i] != 0) {
					noOtherKeyPressed = false;
					break;
				}
			}

			if (noOtherKeyPressed) {
				BMessenger deskbar("application/x-vnd.Be-TSKB");
				if (deskbar.IsValid())
					deskbar.SendMessage('BeMn');
			}
		}

		// Consistency repair: ev.value==1 is a genuine first press (not auto-repeat).
		// If the key is already tracked as down we missed a key-up (QEMU input
		// capture, libevdev/libinput dropping events).  Record the mismatch here
		// so the XKB repair can be done inside the existing fKeymapLock section.
		bool missedKeyUp = (ev.value == 1 && keycode < 128
			&& (states[keycode >> 3] & (1 << (7 - (keycode & 7)))) != 0);
		if (missedKeyUp)
			states[keycode >> 3] &= ~(1 << (7 - (keycode & 7)));

		if (keycode < 128) {
			if (isKeyDown)
				states[(keycode) >> 3] |= (1 << (7 - (keycode & 0x7)));
			else
				states[(keycode) >> 3] &= (~(1 << (7 - (keycode & 0x7))));
		}

		if (isKeyDown && keycode == 111 /* KEY_DELETE */
			&& (states[fCommandKey >> 3] & (1 << (7 - (fCommandKey & 0x7))))
			&& (states[fControlKey >> 3] & (1 << (7 - (fControlKey & 0x7))))) {
			LOG_EVENT("TeamMonitor called\n");

			// show the team monitor
			if (fOwner->fTeamMonitorWindow == NULL)
				fOwner->fTeamMonitorWindow = new(std::nothrow) TeamMonitorWindow();

			if (fOwner->fTeamMonitorWindow != NULL)
				fOwner->fTeamMonitorWindow->Enable();

			ctrlAltDelPressed = true;
		}

		if (ctrlAltDelPressed) {
			if (fOwner->fTeamMonitorWindow != NULL) {
				BMessage message(kMsgCtrlAltDelPressed);
				message.AddBool("key down", isKeyDown);
				fOwner->fTeamMonitorWindow->PostMessage(&message);
			}

			if (!isKeyDown)
				ctrlAltDelPressed = false;
		}

		BAutolock lock(fKeymapLock);

		// Phase 6: xkb modifier tracking and character generation
		xkb_keycode_t xkbCode = keycode + 8;

		// Synthetic release for missed key-up: update XKB and emit
		// B_MODIFIERS_CHANGED before processing the actual press below.
		if (missedKeyUp) {
			xkb_state_update_key(fXkbState, xkbCode, XKB_KEY_UP);
			uint32 repairedMods = 0;
#define _KBIT(c) (states[(c) >> 3] & (1 << (7 - ((c) & 7))))
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_SHIFT,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_SHIFT_KEY;
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CTRL,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_CONTROL_KEY;
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_ALT,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_COMMAND_KEY;
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_LOGO,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_OPTION_KEY;
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CAPS,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_CAPS_LOCK;
			if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_NUM,
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_NUM_LOCK;
			if (xkb_state_mod_name_is_active(fXkbState, "Scroll_Lock",
					XKB_STATE_MODS_EFFECTIVE)) repairedMods |= B_SCROLL_LOCK;
			if (menuKeyDown) repairedMods |= B_MENU_KEY;
			if (_KBIT(42))  repairedMods |= B_LEFT_SHIFT_KEY;
			if (_KBIT(54))  repairedMods |= B_RIGHT_SHIFT_KEY;
			if (_KBIT(29))  repairedMods |= B_LEFT_CONTROL_KEY;
			if (_KBIT(97))  repairedMods |= B_RIGHT_CONTROL_KEY;
			if (_KBIT(56))  repairedMods |= B_LEFT_COMMAND_KEY;
			if (_KBIT(100) && (repairedMods & B_COMMAND_KEY))
				repairedMods |= B_RIGHT_COMMAND_KEY;
			if (_KBIT(125)) repairedMods |= B_LEFT_OPTION_KEY;
			if (_KBIT(126)) repairedMods |= B_RIGHT_OPTION_KEY;
#undef _KBIT
			if (repairedMods != fModifiers) {
				BMessage* repairMsg = new BMessage(B_MODIFIERS_CHANGED);
				if (repairMsg != NULL) {
					repairMsg->AddInt64("when", keyInfo.timestamp);
					repairMsg->AddInt32("be:old_modifiers", fModifiers);
					repairMsg->AddInt32("modifiers", repairedMods);
					repairMsg->AddData("states", B_UINT8_TYPE, states, 16);
					if (fOwner->EnqueueMessage(repairMsg) != B_OK)
						delete repairMsg;
				}
				fModifiers = repairedMods;
			}
		}

		xkb_state_update_key(fXkbState, xkbCode,
			isKeyDown ? XKB_KEY_DOWN : XKB_KEY_UP);

		uint32 oldModifiers = fModifiers;
		uint32 newModifiers = 0;

#define _KBIT(c) (states[(c) >> 3] & (1 << (7 - ((c) & 7))))
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_SHIFT,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_SHIFT_KEY;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CTRL,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_CONTROL_KEY;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_ALT,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_COMMAND_KEY;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_LOGO,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_OPTION_KEY;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CAPS,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_CAPS_LOCK;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_NUM,
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_NUM_LOCK;
		// Scroll Lock: not a standard xkbcommon named mod; track via raw key state
		if (xkb_state_mod_name_is_active(fXkbState, "Scroll_Lock",
				XKB_STATE_MODS_EFFECTIVE)) newModifiers |= B_SCROLL_LOCK;
		if (menuKeyDown) newModifiers |= B_MENU_KEY;
		if (_KBIT(42))  newModifiers |= B_LEFT_SHIFT_KEY;
		if (_KBIT(54))  newModifiers |= B_RIGHT_SHIFT_KEY;
		if (_KBIT(29))  newModifiers |= B_LEFT_CONTROL_KEY;
		if (_KBIT(97))  newModifiers |= B_RIGHT_CONTROL_KEY;
		if (_KBIT(56))  newModifiers |= B_LEFT_COMMAND_KEY;
		// Only set B_RIGHT_COMMAND_KEY for Right Alt when xkb also sees it as
		// the Alt modifier (not when it's acting as AltGr / ISO_Level3_Shift).
		if (_KBIT(100) && (newModifiers & B_COMMAND_KEY))
			newModifiers |= B_RIGHT_COMMAND_KEY;
		if (_KBIT(125)) newModifiers |= B_LEFT_OPTION_KEY;
		if (_KBIT(126)) newModifiers |= B_RIGHT_OPTION_KEY;
#undef _KBIT

		fModifiers = newModifiers;

		if (fModifiers != oldModifiers) {
			BMessage* message = new BMessage(B_MODIFIERS_CHANGED);
			if (message == NULL)
				continue;

			message->AddInt64("when", keyInfo.timestamp);
			message->AddInt32("be:old_modifiers", oldModifiers);
			message->AddInt32("modifiers", fModifiers);
			message->AddData("states", B_UINT8_TYPE, states, 16);

			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;

			if ((newModifiers ^ oldModifiers)
					& (B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK))
				_UpdateLEDs();
		}

		// Character generation via xkb.
		// Call for both key-down and key-up so B_KEY_UP carries the same
		// what as the corresponding B_KEY_DOWN (apps rely on this symmetry).
		char xkbBuf[64] = {};
		xkb_state_key_get_utf8(fXkbState, xkbCode, xkbBuf, sizeof(xkbBuf));
		int32 numBytes = strlen(xkbBuf);

		// Haiku uses LF (0x0a) for Return and BS (0x08) for Backspace.
		// xkbcommon correctly returns CR (0x0d) and DEL (0x7f) per Unicode,
		// but Haiku's API diverges at exactly these two points.
		if (numBytes == 1 && (uint8_t)xkbBuf[0] == 0x0d)
			xkbBuf[0] = 0x0a;
		else if (numBytes == 1 && (uint8_t)xkbBuf[0] == 0x7f)
			xkbBuf[0] = 0x08;

		if (fXkbComposeState != NULL) {
			xkb_compose_state_feed(fXkbComposeState, xkb_state_key_get_one_sym(fXkbState, xkbCode));
			switch (xkb_compose_state_get_status(fXkbComposeState)) {
				case XKB_COMPOSE_COMPOSED:
					numBytes = xkb_compose_state_get_utf8(
						fXkbComposeState, xkbBuf, sizeof(xkbBuf));
					xkb_compose_state_reset(fXkbComposeState);
					break;
				case XKB_COMPOSE_NOTHING:
					break; // use xkb output as-is
				case XKB_COMPOSE_COMPOSING:
					numBytes = 0; // swallow intermediate key
					break;
				case XKB_COMPOSE_CANCELLED:
					xkb_compose_state_reset(fXkbComposeState);
					numBytes = 0;
					break;
			}
		}

		// xkb returns nothing for navigation/function/system keys.
		// Fall back to the Haiku special-character table so apps receive
		// B_KEY_DOWN with the expected byte rather than B_UNMAPPED_KEY_DOWN.
		if (numBytes == 0) {
			for (int i = 0; kSpecialKeys[i].code != 0; i++) {
				if (kSpecialKeys[i].code == keycode) {
					xkbBuf[0] = kSpecialKeys[i].byte0;
					xkbBuf[1] = kSpecialKeys[i].byte1;
					numBytes   = kSpecialKeys[i].byte1 ? 2 : 1;
					xkbBuf[numBytes] = '\0';
					break;
				}
			}
		}

		// Generate Ctrl+letter control characters (0x01–0x1a).
		// xkb returns nothing for these; derive from the base character.
		if (numBytes == 0 && (fModifiers & B_CONTROL_KEY) != 0) {
			char baseBuf[7] = {};
			// Get the character without Ctrl modifier by querying the base level.
			xkb_keysym_t sym = xkb_state_key_get_one_sym(fXkbState, xkbCode);
			uint32_t unicode = xkb_keysym_to_utf32(sym);
			if (unicode >= 'a' && unicode <= 'z') {
				xkbBuf[0] = (char)(unicode - 'a' + 1);
				xkbBuf[1] = '\0';
				numBytes = 1;
			} else if (unicode >= 'A' && unicode <= 'Z') {
				xkbBuf[0] = (char)(unicode - 'A' + 1);
				xkbBuf[1] = '\0';
				numBytes = 1;
			}
		}

		BMessage* msg = new BMessage;
		if (msg == NULL)
			continue;

		if (numBytes > 0)
			msg->what = isKeyDown ? B_KEY_DOWN : B_KEY_UP;
		else
			msg->what = isKeyDown ? B_UNMAPPED_KEY_DOWN : B_UNMAPPED_KEY_UP;

		msg->AddInt64("when", keyInfo.timestamp);
		msg->AddInt32("key", keycode);
		msg->AddInt32("modifiers", fModifiers);
		msg->AddData("states", B_UINT8_TYPE, states, 16);
		if (numBytes > 0) {
			for (int32 i = 0; i < numBytes; i++)
				msg->AddInt8("byte", (int8)xkbBuf[i]);
			msg->AddData("bytes", B_STRING_TYPE, xkbBuf, numBytes + 1);

			// raw_char: base-level character (no modifiers).
			xkb_keysym_t baseSym = XKB_KEY_NoSymbol;
			{
				const xkb_keysym_t* syms;
				int n = xkb_keymap_key_get_syms_by_level(fXkbKeymap,
					xkbCode, 0, 0, &syms);
				if (n > 0)
					baseSym = syms[0];
			}
			uint32 rawChar = (baseSym != XKB_KEY_NoSymbol)
				? xkb_keysym_to_utf32(baseSym) : 0;
			// Non-character keysyms (XKB_KEY_Up, _Left, _Home, F-keys, …)
			// give utf32 == 0. Haiku apps expect raw_char to equal the
			// magic byte produced for these keys (e.g. B_UP_ARROW = 0x1e),
			// so they can do `if (rawChar == B_UP_ARROW)`. Fall back to
			// the byte we just synthesised via kSpecialKeys.
			if (rawChar == 0 && numBytes > 0)
				rawChar = (uint8)xkbBuf[0];
			msg->AddInt32("raw_char", (int32)rawChar);

			if (isKeyDown && lastKeyCode == keycode) {
				repeatCount++;
				msg->AddInt32("be:key_repeat", repeatCount);
			} else
				repeatCount = 1;
		}

		if (msg != NULL && fOwner->EnqueueMessage(msg) != B_OK)
			delete msg;

		lastKeyCode = isKeyDown ? keycode : 0;
	}

	if (consoleFd >= 0)
		close(consoleFd);

	return 0;
}


void
KeyboardDevice::_ControlThreadCleanup()
{
	// NOTE: Only executed when the control thread detected an error
	// and from within the control thread!

	if (fActive) {
		fThread = -1;
		fOwner->_RemoveDevice(fPath);
	} else {
		// In case active is already false, another thread
		// waits for this thread to quit, and may already hold
		// locks that _RemoveDevice() wants to acquire. In another
		// words, the device is already being removed, so we simply
		// quit here.
	}
}


void
KeyboardDevice::_RebuildXkb()
{
	// Tear down any existing xkb state
	if (fXkbState != NULL) {
		xkb_state_unref(fXkbState);
		fXkbState = NULL;
	}
	if (fXkbKeymap != NULL) {
		xkb_keymap_unref(fXkbKeymap);
		fXkbKeymap = NULL;
	}
	if (fXkbContext != NULL) {
		xkb_context_unref(fXkbContext);
		fXkbContext = NULL;
	}
	if (fXkbComposeState != NULL) {
		xkb_compose_state_unref(fXkbComposeState);
		fXkbComposeState = NULL;
	}

	// Read xkb layout from settings file if present.
	// File format (plain text, one key=value per line):
	//   rules=evdev
	//   model=pc105
	//   layout=it
	//   variant=
	//   options=
	char rules[64]    = "evdev";
	char model[64]    = "pc105";
	char layout[64]   = "";
	char variant[64]  = "";
	char options[256] = "";

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("input/xkb_layout");
		FILE* f = fopen(settingsPath.Path(), "r");
		if (f != NULL) {
			char line[512];
			while (fgets(line, sizeof(line), f) != NULL) {
				char* nl = strchr(line, '\n');
				if (nl != NULL) *nl = '\0';
				auto parse = [](const char* ln, const char* key,
					char* out, size_t outLen) {
					size_t klen = strlen(key);
					if (strncmp(ln, key, klen) == 0 && ln[klen] == '=')
						strlcpy(out, ln + klen + 1, outLen);
				};
				parse(line, "rules",   rules,   sizeof(rules));
				parse(line, "model",   model,   sizeof(model));
				parse(line, "layout",  layout,  sizeof(layout));
				parse(line, "variant", variant, sizeof(variant));
				parse(line, "options", options, sizeof(options));
			}
			fclose(f);
		}
	}

	fXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (fXkbContext == NULL)
		return;

	struct xkb_rule_names names = {
		rules[0]   ? rules   : NULL,
		model[0]   ? model   : NULL,
		layout[0]  ? layout  : NULL,
		variant[0] ? variant : NULL,
		options[0] ? options : NULL,
	};
	fXkbKeymap = xkb_keymap_new_from_names(fXkbContext, &names,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (fXkbKeymap == NULL) {
		fprintf(stderr, "KeyboardInputDevice: xkb_keymap_new_from_names failed, "
			"falling back to default\n");
		// Try bare default
		struct xkb_rule_names fallback = { "evdev", "pc105", "us", "", "" };
		fXkbKeymap = xkb_keymap_new_from_names(fXkbContext, &fallback,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	if (fXkbKeymap == NULL) {
		// Cannot continue without a keymap
		return;
	}
		fXkbState = xkb_state_new(fXkbKeymap);
	if (fXkbState == NULL) {
		xkb_keymap_unref(fXkbKeymap);
		fXkbKeymap = NULL;
		return;
	}
	if (fXkbState != NULL) {
		const char* locale = getenv("LANG");
		if (locale == NULL || locale[0] == '\0')
			locale = "C";
		fXkbComposeTable = xkb_compose_table_new_from_locale(
			fXkbContext, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
		if (fXkbComposeTable != NULL) {
			fXkbComposeState = xkb_compose_state_new(
				fXkbComposeTable, XKB_COMPOSE_STATE_NO_FLAGS);
			xkb_compose_table_unref(fXkbComposeTable);
			fXkbComposeTable = NULL;
		}
	}
}


void
KeyboardDevice::_UpdateSettings(uint32 opcode)
{
	CALLED();

	if (opcode == 0 || opcode == B_KEY_REPEAT_RATE_CHANGED)
		get_key_repeat_rate(&fSettings.key_repeat_rate);

	if (opcode == 0 || opcode == B_KEY_REPEAT_DELAY_CHANGED)
		get_key_repeat_delay(&fSettings.key_repeat_delay);

	if (opcode == 0 || opcode == B_KEY_REPEAT_RATE_CHANGED
		|| opcode == B_KEY_REPEAT_DELAY_CHANGED) {
		if (fSettings.key_repeat_rate > 0) {
			unsigned int rep[2] = {
				(unsigned int)(fSettings.key_repeat_delay / 1000),
				(unsigned int)(1000000 / fSettings.key_repeat_rate)
			};
			ioctl(fFD, EVIOCSREP, rep);
		}
	}

	if (opcode == 0 || opcode == B_KEY_MAP_CHANGED
		|| opcode == B_KEY_LOCKS_CHANGED) {
		BAutolock lock(fKeymapLock);
		fKeymap.RetrieveCurrent();
		fModifiers = fKeymap.Map().lock_settings;
		_UpdateLEDs();
		fControlKey = KEY_ControlL;  // 29 = KEY_LEFTCTRL
		fCommandKey = KEY_CmdL;      // 56 = KEY_LEFTALT

		if (opcode == B_KEY_MAP_CHANGED) {
			// Layout changed: rebuild xkb context/keymap/state from the
			// updated settings file so subsequent key events use the new layout.
			_RebuildXkb();
		}
	}
}


void
KeyboardDevice::_UpdateLEDs()
{
	if (fFD < 0)
		return;

	struct input_event leds[3] = {};
	leds[0].type = EV_LED; leds[0].code = LED_NUML;
	leds[0].value = (fModifiers & B_NUM_LOCK)    ? 1 : 0;
	leds[1].type = EV_LED; leds[1].code = LED_CAPSL;
	leds[1].value = (fModifiers & B_CAPS_LOCK)   ? 1 : 0;
	leds[2].type = EV_LED; leds[2].code = LED_SCROLLL;
	leds[2].value = (fModifiers & B_SCROLL_LOCK) ? 1 : 0;
	write(fFD, leds, sizeof(leds));
}


status_t
KeyboardDevice::_EnqueueInlineInputMethod(int32 opcode,
	const char* string, bool confirmed, BMessage* keyDown)
{
	BMessage* message = new BMessage(B_INPUT_METHOD_EVENT);
	if (message == NULL)
		return B_NO_MEMORY;

	message->AddInt32("be:opcode", opcode);
	message->AddBool("be:inline_only", true);

	if (string != NULL)
		message->AddString("be:string", string);
	if (confirmed)
		message->AddBool("be:confirmed", true);
	if (keyDown)
		message->AddMessage("be:translated", keyDown);
	if (opcode == B_INPUT_METHOD_STARTED)
		message->AddMessenger("be:reply_to", this);

	status_t status = fOwner->EnqueueMessage(message);
	if (status != B_OK)
		delete message;

	return status;
}


//	#pragma mark -


KeyboardInputDevice::KeyboardInputDevice()
	:
	fDevices(2),
	fDeviceListLock("KeyboardInputDevice list"),
	fTeamMonitorWindow(NULL)
{
	CALLED();

	StartMonitoringDevice(kKeyboardDevicesDirectory);
	_RecursiveScan(kKeyboardDevicesDirectory);
}


KeyboardInputDevice::~KeyboardInputDevice()
{
	CALLED();

	if (fTeamMonitorWindow) {
		fTeamMonitorWindow->PostMessage(B_QUIT_REQUESTED);
		fTeamMonitorWindow = NULL;
	}

	StopMonitoringDevice(kKeyboardDevicesDirectory);
	fDevices.MakeEmpty();
}


status_t
KeyboardInputDevice::SystemShuttingDown()
{
	CALLED();
	if (fTeamMonitorWindow)
		fTeamMonitorWindow->PostMessage(SYSTEM_SHUTTING_DOWN);

	return B_OK;
}


status_t
KeyboardInputDevice::InitCheck()
{
	CALLED();
	return BInputServerDevice::InitCheck();
}


status_t
KeyboardInputDevice::Start(const char* name, void* cookie)
{
	CALLED();
	TRACE("name %s\n", name);

	KeyboardDevice* device = (KeyboardDevice*)cookie;

	return device->Start();
}


status_t
KeyboardInputDevice::Stop(const char* name, void* cookie)
{
	CALLED();
	TRACE("name %s\n", name);

	KeyboardDevice* device = (KeyboardDevice*)cookie;

	device->Stop();
	return B_OK;
}


status_t
KeyboardInputDevice::Control(const char* name, void* cookie,
	uint32 command, BMessage* message)
{
	CALLED();
	TRACE("KeyboardInputDevice::Control(%s, code: %" B_PRIu32 ")\n", name,
		command);

	if (command == B_NODE_MONITOR)
		_HandleMonitor(message);
	else if (command >= B_KEY_MAP_CHANGED
		&& command <= B_KEY_REPEAT_RATE_CHANGED) {
		KeyboardDevice* device = (KeyboardDevice*)cookie;
		device->UpdateSettings(command);
	}
	return B_OK;
}


status_t
KeyboardInputDevice::_HandleMonitor(BMessage* message)
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


KeyboardDevice*
KeyboardInputDevice::_FindDevice(const char* path) const
{
	for (int i = fDevices.CountItems() - 1; i >= 0; i--) {
		KeyboardDevice* device = fDevices.ItemAt(i);
		if (strcmp(device->Path(), path) == 0)
			return device;
	}

	return NULL;
}


status_t
KeyboardInputDevice::_AddDevice(const char* path)
{
	CALLED();
	TRACE("path: %s\n", path);

	// Only accept keyboard-capable evdev nodes (have EV_KEY + KEY_A,
	// but not BTN_LEFT which would indicate a mouse/pointer device).
	{
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			return B_ERROR;
		struct libevdev* probe = NULL;
		bool isKeyboard = false;
		if (libevdev_new_from_fd(fd, &probe) == 0) {
			isKeyboard = libevdev_has_event_type(probe, EV_KEY)
				&& libevdev_has_event_code(probe, EV_KEY, KEY_A)
				&& !libevdev_has_event_code(probe, EV_KEY, BTN_LEFT);
			libevdev_free(probe);
		}
		close(fd);
		if (!isKeyboard)
			return B_BAD_TYPE;
	}

	BAutolock _(fDeviceListLock);

	_RemoveDevice(path);

	KeyboardDevice* device = new(std::nothrow) KeyboardDevice(this, path);
	if (device == NULL)
		return B_NO_MEMORY;

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	fDevices.AddItem(device);

	return RegisterDevices(devices);
}


status_t
KeyboardInputDevice::_RemoveDevice(const char* path)
{
	BAutolock _(fDeviceListLock);

	KeyboardDevice* device = _FindDevice(path);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	CALLED();
	TRACE("path: %s\n", path);

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	UnregisterDevices(devices);

	fDevices.RemoveItem(device);

	return B_OK;
}


void
KeyboardInputDevice::_RecursiveScan(const char* directory)
{
	CALLED();
	TRACE("directory: %s\n", directory);

	BEntry entry;
	BDirectory dir(directory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (entry.IsDirectory()) {
			// Skip symlink-alias subdirs that cause duplicate registrations
			const char* name = path.Leaf();
			if (strcmp(name, "by-id") == 0 || strcmp(name, "by-path") == 0)
				continue;
			_RecursiveScan(path.Path());
		}
		else
			_AddDevice(path.Path());
	}
}
