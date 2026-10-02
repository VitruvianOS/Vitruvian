/*
 * Copyright 2004-2006, Jérôme Duval. All rights reserved.
 * Copyright 2005-2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2008-2009, Stephan Aßmus, superstippi@gmx.de.
 * Copyright 2026, Dario Casalinuovo, superstippi@gmx.de.
 *
 * Distributed under the terms of the GPL License.
 */


#include "KeyboardInputDevice.h"

#include "UdevDeviceName.h"

#include <errno.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
#include "LinuxKeycodeMap.h"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <AppDefs.h>
#include <private/app/LaunchDaemonDefs.h>
#include <private/app/RegistrarDefs.h>
#include <private/kernel/util/KMessage.h>


#define SETTING_BIT(opcode)	((uint32)1 << (opcode))
#define SETTING_ALL			SETTING_BIT(0)


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

static int32 sNextKeyboardDeviceSerial = 0;


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) KeyboardInputDevice();
}


static int32
device_index_from_name(const BString& name)
{
	const char* digits = name.String();
	digits += strcspn(digits, "0123456789");

	return *digits != '\0' ? atoi(digits) : 0;
}


static char*
get_short_name(const char* longName)
{
	BString string(longName);
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(name, slash + 1, string.Length() - slash);
	int32 index = device_index_from_name(name) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	// Format: "USB" instead of "usb"
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
	fSerial(atomic_add(&sNextKeyboardDeviceSerial, 1)),
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
	fModifiers(0),
	fCommandKey(0),
	fControlKey(0),
	fKeyboardID(0),
	fSettingsCommand(0),
	fSeatCommand(0),
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
			_SyncLocksFromLEDs();
			// Held keys and fSeatModifiers re-seed on the control
			// thread's EVIOCGKEY idle pass, not here.
			break;

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
			BAutolock lock(fKeymapLock);
			fKeymap.RetrieveCurrent();
			_RebuildXkb();
		}
	}
	if (fFD < 0) {
		// Control thread handles device open errors
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

	// Release any modifiers this node was holding before tearing xkb down,
	// so a re-open or unplug cannot leave Shift latched in apps.
	_WithdrawSeatModifiers();

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
		if (find_thread(NULL) == fThread) {
			// Stop() reached via this control thread's own cleanup path
			// (self removal through fOwner->_RemoveDevice()); joining
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
KeyboardDevice::UpdateSettings(uint32 opcode)
{
	CALLED();

	if (fThread < 0)
		return B_ERROR;

	atomic_set(&fSettingsCommand, (int32)opcode);

	return B_OK;
}


void
KeyboardDevice::HandleSeatMessage(uint32 what)
{
	atomic_set(&fSeatCommand, (int32)what);
}


status_t
KeyboardDevice::GetDescription(BMessage* message) const
{
	if (message == NULL)
		return B_BAD_VALUE;

	if (fDescription.IsEmpty())
		return B_NAME_NOT_FOUND;

	status_t status = message->AddString("description", fDescription.String());
	if (status != B_OK)
		return status;

	BString model;
	int32 role = UDEV_ROLE_UNKNOWN;
	udev_device_name(fPath, model, role);
	if (!model.IsEmpty())
		message->AddString("short_description", model);
	message->AddInt32("role", role);

	return B_OK;
}


// #pragma mark - control thread


// evdev -> Haiku key mapping for non-UTF8 keys (xkbcommon provides no UTF8 for nav/function keys)
// For F-keys and system keys: byte0 = B_FUNCTION_KEY, byte1 = B_FN_KEY constant

struct EvdevHaikuChar { uint32 code; uint8 byte0; uint8 byte1; };

static const EvdevHaikuChar kSpecialKeys[] = {
	// Control chars (xkbcommon skips 0x01–0x1f)
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
	// Navigation
	{ KEY_HOME,      B_HOME,        0 },
	{ KEY_END,       B_END,         0 },
	{ KEY_INSERT,    B_INSERT,      0 },
	{ KEY_DELETE,    B_DELETE,      0 },
	{ KEY_PAGEUP,    B_PAGE_UP,     0 },
	{ KEY_PAGEDOWN,  B_PAGE_DOWN,   0 },
	// Keypad navigation
	{ KEY_KP7,       B_HOME,        0 },
	{ KEY_KP8,       B_UP_ARROW,    0 },
	{ KEY_KP9,       B_PAGE_UP,     0 },
	{ KEY_KP4,       B_LEFT_ARROW,  0 },
	{ KEY_KP6,       B_RIGHT_ARROW, 0 },
	{ KEY_KP1,       B_END,         0 },
	{ KEY_KP2,       B_DOWN_ARROW,  0 },
	{ KEY_KP3,       B_PAGE_DOWN,   0 },
	{ KEY_KP0,       B_INSERT,      0 },
	{ KEY_KPDOT,     B_DELETE,      0 },
	// Function keys (byte0 = B_FUNCTION_KEY, byte1 = B_Fn_KEY)
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
	// System keys
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

	_UpdateSettings(SETTING_BIT(0));

	fEpollFd = epoll_create1(0);
	if (fEpollFd < 0) {
		_ControlThreadCleanup();
		return B_ERROR;
	}
	struct epoll_event epev;
	epev.events = EPOLLIN;
	epev.data.fd = fFD;
	epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fFD, &epev);

	// Get keyboard ID via EVIOCGID
	if (fKeyboardID == 0) {
		struct input_id evid;
		if (ioctl(fFD, EVIOCGID, &evid) == 0) {
			fKeyboardID = (uint16)evid.product;
			BMessage message(IS_SET_KEYBOARD_ID);
			message.AddInt16("id", fKeyboardID);
			be_app->PostMessage(&message);
		}
	}

	// Check for real VT (VT_GETSTATE requires root)
	bool hasRealVT = false;
	FILE* vtFile = fopen("/sys/class/tty/tty0/active", "r");
	if (vtFile != NULL) {
		char active[32] = {0};
		hasRealVT = fgets(active, sizeof(active), vtFile) != NULL
			&& strncmp(active, "tty", 3) == 0;
		fclose(vtFile);
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
	// Swallow VT switch key release (lands on target VT)
	uint32 vtSwallowKey = 0;

	raw_key_info keyInfo;
	uint32 lastKeyCode = 0;
	uint32 repeatCount = 1;
	uint8 states[16];
	bool ctrlAltDelPressed = false;

	memset(states, 0, sizeof(states));

	// Roles come from keymap, not fixed keys; ctrl-mode swaps them

	// A re-opened node learns held keys on the first idle pass; keep this
	// the only EVIOCGKEY seeding path (seat enable uses it too).
	bool forceResyncNow = true;

	while (fActive) {
		uint32 pending = (uint32)atomic_get_and_set(&fSettingsCommand, 0);
		if (pending != 0)
			_UpdateSettings(pending);

		int32 seatCmd = atomic_get_and_set(&fSeatCommand, 0);
		if (seatCmd == (int32)B_SEAT_DISABLED) {
			fOwner->fSeatEnabled = false;
			_ReleaseHeldKeys(states, vtLCtrl, vtRCtrl, vtAlt, vtRalt,
				menuKeyDown, ctrlAltDelPressed);
		} else if (seatCmd == (int32)B_SEAT_ENABLED
			|| seatCmd == (int32)B_SYSTEM_RESUMED) {
			// Locks resync now; held keys and the seat union re-seed on the
			// forced idle pass below.
			fOwner->fSeatEnabled = true;
			_SyncLocksFromLEDs();
			forceResyncNow = true;
		}

		// Drain libevdev queue before epoll wait (prevents lost key-up events)
		struct epoll_event fired;
		if (libevdev_has_event_pending(fInputHandle) <= 0
			&& epoll_wait(fEpollFd, &fired, 1, forceResyncNow ? 0 : 100) <= 0) {
			forceResyncNow = false;
			// Reconcile shadow state with kernel via EVIOCGKEY (recovers dropped UP events)
#ifndef EVIOCGKEY
#define EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, len)
#endif
			if (fFD < 0)
				continue;
			// 128 bytes = 1024 bits (KEY_MAX ≤ 767)
			uint8_t keyBits[128] = {};
			if (ioctl(fFD, EVIOCGKEY(128), keyBits) < 0)
				continue;

#define _HWBIT(c) ((keyBits[(c) >> 3] >> ((c) & 7)) & 1)
#define _SHBIT(c) (states[(c) >> 3] & (1 << (7 - ((c) & 7))))

			// Ctrl+Alt+Del recovery
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

			// Release keys the hardware reports up, seed ones it reports down.
			// Seed only while the seat is enabled, or a VT-switch release returns.
			bool seedHeld = fOwner->fSeatEnabled;
			bool anyRepaired = false;
			for (uint32 k = 1; k < 128; k++) {
				uint32 haiku = linux_to_haiku_keycode(k);
				bool hwDown = _HWBIT(k) != 0;
				bool shDown = haiku != 0 && haiku < 128 && _SHBIT(haiku);
				if (hwDown == shDown)
					continue;
				if (shDown && !hwDown) {
					if (!anyRepaired) {
						fKeymapLock.Lock();
						anyRepaired = true;
					}
					states[haiku >> 3] &= ~(1 << (7 - (haiku & 7)));
					xkb_state_update_key(fXkbState, k + 8, XKB_KEY_UP);

					if (k == KEY_LEFTCTRL)       vtLCtrl = false;
					else if (k == KEY_RIGHTCTRL) vtRCtrl = false;
					else if (k == KEY_LEFTALT)   vtAlt = false;
					else if (k == KEY_RIGHTALT)  vtRalt = false;
					else if (k == KEY_Menu)      menuKeyDown = false;

					BMessage* upMsg = new(std::nothrow) BMessage(B_UNMAPPED_KEY_UP);
					if (upMsg != NULL) {
						upMsg->AddInt64("when", system_time());
						upMsg->AddInt32("key", haiku);
						upMsg->AddInt32("modifiers", fModifiers);
						upMsg->AddData("states", B_UINT8_TYPE, states, 16);
						if (fOwner->EnqueueMessage(upMsg) != B_OK)
							delete upMsg;
					}
				} else if (hwDown && !shDown && haiku != 0 && haiku < 128
					&& seedHeld) {
					if (!anyRepaired) {
						fKeymapLock.Lock();
						anyRepaired = true;
					}
					states[haiku >> 3] |= (1 << (7 - (haiku & 7)));
					xkb_state_update_key(fXkbState, k + 8, XKB_KEY_DOWN);
				}
			}

			if (anyRepaired) {
				uint32 oldModifiers = fModifiers;
				uint32 newModifiers = _ComputeModifiers(states, menuKeyDown);
				uint32 report = _PublishModifiers(newModifiers);
				if (report != oldModifiers) {
					BMessage* m = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
					if (m != NULL) {
						m->AddInt64("when", system_time());
						m->AddInt32("be:old_modifiers", oldModifiers);
						m->AddInt32("modifiers", report);
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

		// Track raw modifiers for VT switching and Menu key (keymap-independent)
		if (ev.code == KEY_LEFTCTRL)       vtLCtrl   = isKeyDown;
		else if (ev.code == KEY_RIGHTCTRL) vtRCtrl   = isKeyDown;
		else if (ev.code == KEY_LEFTALT)   vtAlt     = isKeyDown;
		else if (ev.code == KEY_RIGHTALT)  vtRalt    = isKeyDown;
		else if (ev.code == KEY_Menu)      menuKeyDown = isKeyDown;

		if (!isKeyDown && ev.code == vtSwallowKey) {
			vtSwallowKey = 0;
			continue;
		}

		// VT switch: Ctrl+Alt+Fn (native) or Alt+Fn (VM)
		if (hasRealVT && isKeyDown) {
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
					port_id janusPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
					if (janusPort >= 0) {
						BPrivate::KMessage msg(BPrivate::B_JANUS_SWITCH_VT);
						msg.AddInt32("vt", (int32)fn);
						// No reply (would stall evdev loop)
						msg.SendTo(janusPort, -1, (BPrivate::KMessage*)NULL);
					} else {
						fprintf(stderr, "KeyboardInputDevice: janus port not found; can't switch VT\n");
					}

					// Clear modifier latches (releases go to target VT)
					vtLCtrl = vtRCtrl = vtAlt = vtRalt = false;
					vtSwallowKey = ev.code;
					continue;
				}
			}
		}

		// Power management: send shutdown/reboot to registrar on release
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

		uint32 haikuKey = linux_to_haiku_keycode(keycode);

		// Repair: ev.value==1 is first press (not repeat); if key already down, we missed UP event
		bool missedKeyUp = (ev.value == 1 && haikuKey != 0 && haikuKey < 128
			&& (states[haikuKey >> 3] & (1 << (7 - (haikuKey & 7)))) != 0);
		if (missedKeyUp)
			states[haikuKey >> 3] &= ~(1 << (7 - (haikuKey & 7)));

		if (haikuKey != 0 && haikuKey < 128) {
			if (isKeyDown)
				states[haikuKey >> 3] |= (1 << (7 - (haikuKey & 0x7)));
			else
				states[haikuKey >> 3] &= (~(1 << (7 - (haikuKey & 0x7))));
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

#define _CADBIT(c) ((states[(c) >> 3] & (1 << (7 - ((c) & 7)))) != 0)
			bool delHeld = _CADBIT(linux_to_haiku_keycode(111));
			bool ctrlHeld = _CADBIT(fControlKey);
			bool cmdHeld = _CADBIT(fCommandKey);
#undef _CADBIT
			if (!delHeld || !ctrlHeld || !cmdHeld)
				ctrlAltDelPressed = false;
		}

		BAutolock lock(fKeymapLock);

		// Phase 6: xkb modifier tracking and character generation
		xkb_keycode_t xkbCode = keycode + 8;

		// Synthetic release for missed key-up: update XKB and emit
		// B_MODIFIERS_CHANGED before processing the actual press below.
		if (missedKeyUp) {
			xkb_state_update_key(fXkbState, xkbCode, XKB_KEY_UP);
			uint32 before = fModifiers;
			uint32 repairedMods = _ComputeModifiers(states, menuKeyDown);
			uint32 report = _PublishModifiers(repairedMods);
			if (report != before) {
				BMessage* repairMsg = new BMessage(B_MODIFIERS_CHANGED);
				if (repairMsg != NULL) {
					repairMsg->AddInt64("when", keyInfo.timestamp);
					repairMsg->AddInt32("be:old_modifiers", before);
					repairMsg->AddInt32("modifiers", report);
					repairMsg->AddData("states", B_UINT8_TYPE, states, 16);
					if (fOwner->EnqueueMessage(repairMsg) != B_OK)
						delete repairMsg;
				}
			}
		}

		// xkb counts every DOWN; a repeat fed as a press needs its own
		// release, so a held Shift would stay latched after the key-up.
		if (ev.value != 2) {
			xkb_state_update_key(fXkbState, xkbCode,
				isKeyDown ? XKB_KEY_DOWN : XKB_KEY_UP);
		}

		uint32 oldModifiers = fModifiers;
		uint32 newModifiers = _ComputeModifiers(states, menuKeyDown);
		uint32 report = _PublishModifiers(newModifiers);

		// A modifier key-up clears its seat bits even if this node never
		// saw the matching down (VirtualBox can split one keyboard).
		if (!isKeyDown) {
			uint32 clearBits = 0;
			const key_map& map = fKeymap.Map();
			if (keycode == KEY_LEFTSHIFT)
				clearBits |= B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
			else if (keycode == KEY_RIGHTSHIFT)
				clearBits |= B_SHIFT_KEY | B_RIGHT_SHIFT_KEY;
			if (haikuKey == map.left_control_key)
				clearBits |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
			if (haikuKey == map.right_control_key)
				clearBits |= B_CONTROL_KEY | B_RIGHT_CONTROL_KEY;
			if (haikuKey == map.left_command_key)
				clearBits |= B_COMMAND_KEY | B_LEFT_COMMAND_KEY;
			if (haikuKey == map.right_command_key)
				clearBits |= B_COMMAND_KEY | B_RIGHT_COMMAND_KEY;
			if (haikuKey == map.left_option_key)
				clearBits |= B_OPTION_KEY | B_LEFT_OPTION_KEY;
			if (haikuKey == map.right_option_key)
				clearBits |= B_OPTION_KEY | B_RIGHT_OPTION_KEY;
			if (clearBits != 0) {
				BAutolock seatLock(fOwner->fSeatModifierLock);
				fOwner->fSeatModifiers &= ~clearBits;
				fModifiers = fOwner->fSeatModifiers;
				report = fModifiers;
			}
		}

		if (report != oldModifiers) {
			BMessage* message = new BMessage(B_MODIFIERS_CHANGED);
			if (message == NULL)
				continue;

			message->AddInt64("when", keyInfo.timestamp);
			message->AddInt32("be:old_modifiers", oldModifiers);
			message->AddInt32("modifiers", report);
			message->AddData("states", B_UINT8_TYPE, states, 16);

			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;

			if ((newModifiers ^ oldModifiers)
					& (B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK))
				_UpdateLEDs();
		}

		// Character generation via xkb (called for both key-down and key-up for symmetry)
		char xkbBuf[64] = {};
		xkb_state_key_get_utf8(fXkbState, xkbCode, xkbBuf, sizeof(xkbBuf));
		int32 numBytes = strlen(xkbBuf);

		// Haiku uses LF (0x0a) for Return; xkb reports CR (0x0d)
		if (numBytes == 1 && (uint8_t)xkbBuf[0] == 0x0d)
			xkbBuf[0] = 0x0a;

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

		// xkb returns nothing for nav/function/system keys; fall back to special-character table
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

		// Generate Ctrl+letter control chars (0x01–0x1a) from base character
		if (numBytes == 0 && (fModifiers & B_CONTROL_KEY) != 0) {
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

		uint32 msgKey = (haikuKey != 0) ? haikuKey
			: (keycode >= 0x80 ? keycode : 0);

		msg->AddInt64("when", keyInfo.timestamp);
		msg->AddInt32("key", msgKey);
		msg->AddInt32("modifiers", fModifiers);
		msg->AddData("states", B_UINT8_TYPE, states, 16);
		if (numBytes > 0) {
			for (int32 i = 0; i < numBytes; i++)
				msg->AddInt8("byte", (int8)xkbBuf[i]);
			msg->AddData("bytes", B_STRING_TYPE, xkbBuf, numBytes + 1);

			// raw_char: base-level character (no modifiers)
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
			// Non-character keysyms (Up, Left, Home, F-keys) give utf32==0; fall back to kSpecialKeys
			if (rawChar == 0 && numBytes > 0)
				rawChar = (uint8)xkbBuf[0];
			if (rawChar == 0x0d)
				rawChar = B_ENTER;
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

	return 0;
}


void
KeyboardDevice::_ControlThreadCleanup()
{
	// Called from control thread on error only.
	//
	// Do NOT pre-clear fThread here: Stop() (invoked from the delete this
	// triggers, on whichever thread ends up performing it) tells self-
	// removal apart from external removal by comparing fThread against
	// find_thread(), not by a flag on `this`. That keeps `this` valid for
	// the snapshot below no matter which thread wins the race, since a
	// joining Stop() can only return once this thread has truly finished.

	// Withdraw first: a node that dies mid-hold would otherwise leave
	// Shift stuck for every other device on the seat.
	_WithdrawSeatModifiers();

	if (fActive) {
		char path[B_PATH_NAME_LENGTH];
		strlcpy(path, fPath, sizeof(path));
		// Use serial not path: fast replug may swap in replacement.
		int32 serial = fSerial;
		fOwner->_RemoveDevice(path, serial);
	} else {
		// Device already being removed by another thread.
	}
}


uint32
KeyboardDevice::_ComputeModifiers(const uint8* states, bool menuKeyDown)
{
	uint32 mods = 0;
#define _KBIT(c) ((c) != 0 && (c) < 128 \
	&& (states[(c) >> 3] & (1 << (7 - ((c) & 7)))))
	if (fXkbState != NULL) {
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_SHIFT,
				XKB_STATE_MODS_EFFECTIVE) > 0)
			mods |= B_SHIFT_KEY;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_CAPS,
				XKB_STATE_MODS_EFFECTIVE) > 0)
			mods |= B_CAPS_LOCK;
		if (xkb_state_mod_name_is_active(fXkbState, XKB_MOD_NAME_NUM,
				XKB_STATE_MODS_EFFECTIVE) > 0)
			mods |= B_NUM_LOCK;
		if (xkb_state_led_name_is_active(fXkbState,
				XKB_LED_NAME_SCROLL) > 0)
			mods |= B_SCROLL_LOCK;
	}
	if (menuKeyDown)
		mods |= B_MENU_KEY;
	if (_KBIT(linux_to_haiku_keycode(KEY_LEFTSHIFT)))
		mods |= B_LEFT_SHIFT_KEY;
	if (_KBIT(linux_to_haiku_keycode(KEY_RIGHTSHIFT)))
		mods |= B_RIGHT_SHIFT_KEY;
	const key_map& map = fKeymap.Map();
	if (_KBIT(map.left_control_key))
		mods |= B_LEFT_CONTROL_KEY;
	if (_KBIT(map.right_control_key))
		mods |= B_RIGHT_CONTROL_KEY;
	if (_KBIT(map.left_command_key))
		mods |= B_LEFT_COMMAND_KEY;
	if (_KBIT(map.right_command_key))
		mods |= B_RIGHT_COMMAND_KEY;
	if (_KBIT(map.left_option_key))
		mods |= B_LEFT_OPTION_KEY;
	if (_KBIT(map.right_option_key))
		mods |= B_RIGHT_OPTION_KEY;
	if (mods & (B_LEFT_CONTROL_KEY | B_RIGHT_CONTROL_KEY))
		mods |= B_CONTROL_KEY;
	if (mods & (B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY))
		mods |= B_COMMAND_KEY;
	if (mods & (B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY))
		mods |= B_OPTION_KEY;
#undef _KBIT
	return mods;
}


// Fold this device's transition into the seat and return what apps see.
// A key-up clears seat-wide even if another evdev node never saw the down.
uint32
KeyboardDevice::_PublishModifiers(uint32 newModifiers)
{
	const uint32 kLocks = B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK;
	uint32 oldModifiers = fModifiers;

	BAutolock _(fOwner->fSeatModifierLock);
	fOwner->fSeatModifiers |= (newModifiers & ~oldModifiers);
	fOwner->fSeatModifiers &= ~(oldModifiers & ~newModifiers);
	fOwner->fSeatModifiers &= ~kLocks;
	fOwner->fSeatModifiers |= (newModifiers & kLocks);
	fModifiers = fOwner->fSeatModifiers;
	return fModifiers;
}


void
KeyboardDevice::_WithdrawSeatModifiers()
{
	// Dying mid-hold: withdraw held modifiers so Shift doesn't latch for
	// everyone. Locks stay; the kernel LED owns them.
	if (fModifiers == 0 || fOwner == NULL)
		return;

	const uint32 kLocks = B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK;
	uint32 oldModifiers = fModifiers;
	uint32 seat;
	{
		BAutolock _(fOwner->fSeatModifierLock);
		fOwner->fSeatModifiers &= ~(oldModifiers & ~kLocks);
		seat = fOwner->fSeatModifiers;
		fModifiers = 0;
	}

	BMessage* message = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
	if (message == NULL)
		return;
	message->AddInt64("when", system_time());
	message->AddInt32("be:old_modifiers", oldModifiers);
	message->AddInt32("modifiers", seat);
	uint8_t zeroStates[16] = {};
	message->AddData("states", B_UINT8_TYPE, zeroStates, 16);
	if (fOwner->EnqueueMessage(message) != B_OK)
		delete message;
}


// xkb can only toggle locks, tap when LED disagrees with state
void
KeyboardDevice::_SyncLocksFromLEDs()
{
	if (fFD < 0 || fXkbState == NULL || fXkbKeymap == NULL)
		return;

	uint8 leds[LED_MAX / 8 + 1] = {};
	if (ioctl(fFD, EVIOCGLED(sizeof(leds)), leds) < 0)
		return;

	static const struct {
		uint32		ledBit;
		const char*	keyName;
		const char*	ledName;
	} kLocks[] = {
		{ LED_CAPSL,   "CAPS", XKB_LED_NAME_CAPS },
		{ LED_NUML,    "NMLK", XKB_LED_NAME_NUM },
		{ LED_SCROLLL, "SCLK", XKB_LED_NAME_SCROLL },
	};

	for (size_t i = 0; i < sizeof(kLocks) / sizeof(kLocks[0]); i++) {
		bool ledOn = (leds[kLocks[i].ledBit / 8]
			& (1 << (kLocks[i].ledBit % 8))) != 0;
		bool lockOn = xkb_state_led_name_is_active(fXkbState,
			kLocks[i].ledName) > 0;
		if (ledOn == lockOn)
			continue;

		xkb_keycode_t code = xkb_keymap_key_by_name(fXkbKeymap,
			kLocks[i].keyName);
		if (code == XKB_KEYCODE_INVALID)
			continue;

		xkb_state_update_key(fXkbState, code, XKB_KEY_DOWN);
		xkb_state_update_key(fXkbState, code, XKB_KEY_UP);
	}
}


// B_SEAT_DISABLED: the seat is gone until B_SEAT_ENABLED, so release everything the shadow
// state still has down and apps never see a key or modifier stuck across a VT switch.
void
KeyboardDevice::_ReleaseHeldKeys(uint8* states, bool& vtLCtrl, bool& vtRCtrl,
	bool& vtAlt, bool& vtRalt, bool& menuKeyDown, bool& ctrlAltDelPressed)
{
	BAutolock lock(fKeymapLock);

	for (uint32 haiku = 1; haiku < 128; haiku++) {
		if (!(states[haiku >> 3] & (1 << (7 - (haiku & 7)))))
			continue;

		BMessage* upMsg = new(std::nothrow) BMessage(B_UNMAPPED_KEY_UP);
		if (upMsg != NULL) {
			upMsg->AddInt64("when", system_time());
			upMsg->AddInt32("key", haiku);
			upMsg->AddInt32("modifiers", fModifiers);
			upMsg->AddData("states", B_UINT8_TYPE, states, 16);
			if (fOwner->EnqueueMessage(upMsg) != B_OK)
				delete upMsg;
		}
	}
	memset(states, 0, 16);

	// Rebuild xkb state fresh: cheaper and more certain than walking back
	// every modifier/lock key it might have latched.
	if (fXkbState != NULL && fXkbKeymap != NULL) {
		xkb_state_unref(fXkbState);
		fXkbState = xkb_state_new(fXkbKeymap);
	}

	vtLCtrl = vtRCtrl = vtAlt = vtRalt = menuKeyDown = false;
	ctrlAltDelPressed = false;

	const uint32 kLocks = B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK;
	uint32 oldModifiers = fModifiers;
	// Locks are toggle state; keep them. Drop held bits from the seat union
	// too, or a modifier held across a VT switch stays latched seat-wide.
	{
		BAutolock _(fOwner->fSeatModifierLock);
		fOwner->fSeatModifiers &= ~(oldModifiers & ~kLocks);
		fModifiers = oldModifiers & kLocks;
	}
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
	}
}


void
KeyboardDevice::_RebuildXkb()
{
	// Tear down existing xkb state
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

	// Resolution order (never merged)
	char rules[64]    = "evdev";
	char model[64]    = "pc105";
	char layout[64]   = "";
	char variant[64]  = "";
	char options[256] = "";

	bool haveLayout = false;
	const char* source = "compiled-in default";

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		BPath layoutNamePath(settingsPath);
		layoutNamePath.Append("input/layout");
		FILE* f = fopen(layoutNamePath.Path(), "r");
		if (f != NULL) {
			char name[B_FILE_NAME_LENGTH] = "";
			if (fgets(name, sizeof(name), f) != NULL) {
				char* nl = strchr(name, '\n');
				if (nl != NULL) *nl = '\0';
				if (name[0] != '\0') {
					const char* derivedLayout;
					const char* derivedVariant;
					look_up_xkb_layout(name, derivedLayout, derivedVariant);
					strlcpy(layout, derivedLayout, sizeof(layout));
					strlcpy(variant, derivedVariant, sizeof(variant));

					BAutolock lock(fKeymapLock);
					strlcpy(options,
						look_up_xkb_modifier_options(fKeymap.Map()),
						sizeof(options));

					haveLayout = true;
					source = "input/layout";
				}
			}
			fclose(f);
		}
	}

	if (!haveLayout && find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath)
			== B_OK) {
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
			haveLayout = true;
			source = "xkb_layout cache";
		}
	}

	if (!haveLayout) {
		FILE* f = fopen("/etc/default/keyboard", "r");
		if (f != NULL) {
			char line[512];
			while (fgets(line, sizeof(line), f) != NULL) {
				char* nl = strchr(line, '\n');
				if (nl != NULL) *nl = '\0';
				auto parse = [](const char* ln, const char* key,
					char* out, size_t outLen) {
					size_t klen = strlen(key);
					if (strncmp(ln, key, klen) != 0 || ln[klen] != '=')
						return;
					const char* value = ln + klen + 1;
					size_t len = strlen(value);
					if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
						value++;
						len -= 2;
					}
					if (len >= outLen)
						len = outLen - 1;
					memcpy(out, value, len);
					out[len] = '\0';
				};
				parse(line, "XKBMODEL",   model,   sizeof(model));
				parse(line, "XKBLAYOUT",  layout,  sizeof(layout));
				parse(line, "XKBVARIANT", variant, sizeof(variant));
				parse(line, "XKBOPTIONS", options, sizeof(options));
			}
			fclose(f);
			source = "/etc/default/keyboard";
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

	fprintf(stderr, "KeyboardInputDevice: xkb rebuild rules=%s model=%s "
		"layout=%s variant=%s options=%s source=%s -> %s\n",
		rules, model, layout, variant, options, source,
		fXkbKeymap != NULL ? "ok" : "FAILED");

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

	_SyncLocksFromLEDs();
}


void
KeyboardDevice::_UpdateSettings(uint32 pending)
{
	CALLED();

	if ((pending & (SETTING_ALL
			| SETTING_BIT(B_KEY_REPEAT_RATE_CHANGED))) != 0) {
		get_key_repeat_rate(&fSettings.key_repeat_rate);
	}

	if ((pending & (SETTING_ALL
			| SETTING_BIT(B_KEY_REPEAT_DELAY_CHANGED))) != 0) {
		get_key_repeat_delay(&fSettings.key_repeat_delay);
	}

	if ((pending & (SETTING_ALL | SETTING_BIT(B_KEY_REPEAT_RATE_CHANGED)
			| SETTING_BIT(B_KEY_REPEAT_DELAY_CHANGED))) != 0) {
		if (fSettings.key_repeat_rate > 0) {
			// EVIOCSREP's rep[] is milliseconds, not microseconds.
			unsigned int rep[2] = {
				(unsigned int)(fSettings.key_repeat_delay / 1000),
				(unsigned int)(10000 / fSettings.key_repeat_rate)
			};
			if (ioctl(fFD, EVIOCSREP, rep) < 0) {
				fprintf(stderr, "KeyboardInputDevice: EVIOCSREP failed: %s\n",
					strerror(errno));
			}
		}
	}

	if ((pending & (SETTING_ALL | SETTING_BIT(B_KEY_MAP_CHANGED)
			| SETTING_BIT(B_KEY_LOCKS_CHANGED))) != 0) {
		BAutolock lock(fKeymapLock);
		fKeymap.RetrieveCurrent();
		// Merge only the lock bits, or this stomps modifiers held mid-keypress.
		const uint32 kLockBits = B_CAPS_LOCK | B_SCROLL_LOCK | B_NUM_LOCK;
		fModifiers = (fModifiers & ~kLockBits)
			| (fKeymap.Map().lock_settings & kLockBits);
		_UpdateLEDs();
		fControlKey = linux_to_haiku_keycode(KEY_ControlL);
		fCommandKey = linux_to_haiku_keycode(KEY_CmdL);

		// SETTING_ALL is excluded: Start() already built the xkb state.
		if ((pending & SETTING_BIT(B_KEY_MAP_CHANGED)) != 0) {
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
	fTeamMonitorWindow(NULL),
	fSeatModifiers(0),
	fSeatModifierLock("keyboard seat modifiers"),
	fSeatEnabled(true)
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

	// Detach every device under the lock, then delete them outside it:
	// ~KeyboardDevice() may join a control thread via Stop(), and joining
	// while fDeviceListLock is held can deadlock against that same
	// thread's own self-removal call (see _ControlThreadCleanup()).
	BObjectList<KeyboardDevice, false> doomed;
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
		KeyboardDevice* device = doomed.ItemAt(i);

		input_device_ref* devices[2];
		devices[0] = device->DeviceRef();
		devices[1] = NULL;
		UnregisterDevices(devices);

		delete device;
	}
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
		return _HandleMonitor(message);
	else if (command >= B_KEY_MAP_CHANGED
		&& command <= B_KEY_REPEAT_RATE_CHANGED) {
		KeyboardDevice* device = (KeyboardDevice*)cookie;
		device->UpdateSettings(command);
	} else if (command == B_GET_DEVICE_DESCRIPTION) {
		KeyboardDevice* device = (KeyboardDevice*)cookie;
		return device->GetDescription(message);
	} else if (command == B_SEAT_DISABLED || command == B_SEAT_ENABLED
		|| command == B_SYSTEM_RESUMED) {
		KeyboardDevice* device = (KeyboardDevice*)cookie;
		device->HandleSeatMessage(command);
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

	// The node monitor hands us any entry under /dev/input, not just
	// device nodes.
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
		return B_BAD_TYPE;

	// Only accept keyboard-capable evdev nodes (have EV_KEY + KEY_A,
	// but not BTN_LEFT which would indicate a mouse/pointer device).
	BString description;
	{
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			// udev widens permissions shortly after the node appears;
			// report distinctly so AddOnManager retries.
			return (errno == EACCES || errno == EPERM)
				? B_PERMISSION_DENIED : B_ERROR;
		}
		struct libevdev* probe = NULL;
		bool isKeyboard = false;
		if (libevdev_new_from_fd(fd, &probe) == 0) {
			isKeyboard = libevdev_has_event_type(probe, EV_KEY)
				&& libevdev_has_event_code(probe, EV_KEY, KEY_A)
				&& !libevdev_has_event_code(probe, EV_KEY, BTN_LEFT);
			if (isKeyboard) {
				const char* deviceName = libevdev_get_name(probe);
				if (deviceName != NULL)
					description = deviceName;
			}
			libevdev_free(probe);
		}
		close(fd);
		if (!isKeyboard)
			return B_BAD_TYPE;
	}

	// Detach and delete any stale device already at this path (fast
	// replug) before the lock below is taken: _RemoveDevice() may join
	// that device's control thread via Stop(), and that must happen
	// without fDeviceListLock held, see _DetachDevice().
	_RemoveDevice(path);

	BAutolock _(fDeviceListLock);

	KeyboardDevice* device = new(std::nothrow) KeyboardDevice(this, path);
	if (device == NULL)
		return B_NO_MEMORY;

	device->SetDescription(description.String());

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	fDevices.AddItem(device);

	return RegisterDevices(devices);
}


KeyboardDevice*
KeyboardInputDevice::_DetachDevice(const char* path, const int32* serial)
{
	KeyboardDevice* device = NULL;
	{
		BAutolock _(fDeviceListLock);

		device = _FindDevice(path);
		if (device == NULL)
			return NULL;

		// ABA guard: a replacement can land on the same address, so
		// serial, not pointer.
		if (serial != NULL && device->Serial() != *serial) {
			TRACE("%s: stale removal (serial %ld != %ld), ignoring\n", path,
				(long)*serial, (long)device->Serial());
			return NULL;
		}

		CALLED();
		TRACE("path: %s\n", path);

		// Detach only, don't delete: the caller deletes outside the lock,
		// so ~KeyboardDevice()'s Stop() can join the control thread
		// without holding fDeviceListLock. Holding it there would
		// deadlock against the control thread's own self-removal call
		// into _RemoveDevice(), which needs the same lock (see
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

	UnregisterDevices(devices);

	return device;
}


status_t
KeyboardInputDevice::_RemoveDevice(const char* path)
{
	KeyboardDevice* device = _DetachDevice(path, NULL);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	delete device;
	return B_OK;
}


status_t
KeyboardInputDevice::_RemoveDevice(const char* path, int32 serial)
{
	KeyboardDevice* device = _DetachDevice(path, &serial);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	delete device;
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
