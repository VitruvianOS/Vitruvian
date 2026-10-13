/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */

#include "event_adapter.h"

#include "logging.h"
#include "process_manager.h"

#include <AppDefs.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Point.h>
#include <View.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <wpe/wpe.h>


// WebKit runs on the GLib thread: every event goes to WPE from there.
template<typename Event>
static void
dispatch_on_glib(struct wpe_view_backend* backend, const Event& event,
	void (*dispatch)(struct wpe_view_backend*, Event*))
{
	struct Call {
		struct wpe_view_backend*	backend;
		Event						event;
		void (*dispatch)(struct wpe_view_backend*, Event*);
	};
	Call* call = new Call{backend, event, dispatch};
	ProcessManager::Instance().InvokeOnGLibThread([](void* data) -> gboolean {
		Call* call = static_cast<Call*>(data);
		call->dispatch(call->backend, &call->event);
		delete call;
		return G_SOURCE_REMOVE;
	}, call);
}


//	#pragma mark - Constructor/Destructor


EventAdapter::EventAdapter(struct wpe_view_backend* backend)
	:
	fBackend(backend),
	fPressedButton(0)
{
	log_info("EventAdapter: created");
}


EventAdapter::~EventAdapter()
{
	ReleaseAllKeys();
}


//	#pragma mark - Private


// V\OS hybrid keycode space: <=0x7f is the legacy keycode, not an
// evdev code and not the ASCII byte constants in InterfaceDefs.h.
// Values match the input_server mapping (see LinuxKeycodeMap.h).
enum {
	kLegacyEscKey		= 0x01,
	kLegacyPrintKey		= 0x0e,
	kLegacyScrollLockKey	= 0x0f,
	kLegacyPauseKey		= 0x10,
	kLegacyBacktickKey	= 0x11,
	kLegacyBackspaceKey	= 0x1e,
	kLegacyInsertKey		= 0x1f,
	kLegacyHomeKey		= 0x20,
	kLegacyPageUpKey		= 0x21,
	kLegacyNumLockKey	= 0x22,
	kLegacyTabKey		= 0x26,
	kLegacyDeleteKey		= 0x34,
	kLegacyEndKey		= 0x35,
	kLegacyPageDownKey	= 0x36,
	kLegacyCapsLockKey	= 0x3b,
	kLegacyEnterKey		= 0x47,
	kLegacyLeftShiftKey	= 0x4b,
	kLegacyRightShiftKey	= 0x56,
	kLegacyUpKey			= 0x57,
	kLegacyKPEnterKey	= 0x5b,
	kLegacyLeftCtrlKey	= 0x5c,
	kLegacyLeftAltKey	= 0x5d,
	kLegacyRightAltKey	= 0x5f,
	kLegacyRightCtrlKey	= 0x60,
	kLegacyLeftKey		= 0x61,
	kLegacyDownKey		= 0x62,
	kLegacyRightKey		= 0x63,
	kLegacyLeftMetaKey	= 0x66,
	kLegacyRightMetaKey	= 0x67,
	kLegacyMenuKey		= 0x68
};


// X11 keysyms (libwpe key_code space).
enum {
	kXK_Escape		= 0xff1b,
	kXK_Space		= 0x0020,
	kXK_BackSpace	= 0xff08,
	kXK_Tab			= 0xff09,
	kXK_Return		= 0xff0d,
	kXK_KP_Enter	= 0xff8d,
	kXK_Escape_		= 0xff1b,
	kXK_Home		= 0xff50,
	kXK_Left		= 0xff51,
	kXK_Up			= 0xff52,
	kXK_Right		= 0xff53,
	kXK_Down		= 0xff54,
	kXK_Page_Up		= 0xff55,
	kXK_Page_Down	= 0xff56,
	kXK_End			= 0xff57,
	kXK_Insert		= 0xff63,
	kXK_Delete		= 0xffff,
	kXK_Print		= 0xff61,
	kXK_Scroll_Lock	= 0xff14,
	kXK_Pause		= 0xff13,
	kXK_Num_Lock	= 0xff7f,
	kXK_Caps_Lock	= 0xffe5,
	kXK_Shift_L		= 0xffe1,
	kXK_Shift_R		= 0xffe2,
	kXK_Control_L	= 0xffe3,
	kXK_Control_R	= 0xffe4,
	kXK_Alt_L		= 0xffe9,
	kXK_Alt_R		= 0xffea,
	kXK_Super_L		= 0xffeb,
	kXK_Super_R		= 0xffec,
	kXK_Menu		= 0xff67,
	kXK_F1			= 0xffbe,
	kXK_F12			= 0xffc9
};


// Raw evdev codes the input server passes through untranslated (>=0x80)
// plus media keys that have no legacy keycode.
enum {
	kEvdevPrintScreen	= 99,
	kEvdevLeftCtrl		= 29,
	kEvdevMenu			= 139,
	kEvdevPlayPause		= 164,
	kEvdevNextTrack		= 163,
	kEvdevPrevTrack		= 165,
	kEvdevStop			= 166,
	kEvdevMute			= 113,
	kEvdevVolumeDown	= 114,
	kEvdevVolumeUp		= 115
};

enum {
	kXK_XF86AudioPlay		= 0x1008ff14,
	kXK_XF86AudioStop		= 0x1008ff15,
	kXK_XF86AudioNext		= 0x1008ff17,
	kXK_XF86AudioPrev		= 0x1008ff16,
	kXK_XF86AudioMute		= 0x1008ff12,
	kXK_XF86AudioLower		= 0x1008ff11,
	kXK_XF86AudioRaise		= 0x1008ff13
};


uint32_t
EventAdapter::_DecodeUTF8(const char* bytes, int32 numBytes)
{
	if (bytes == NULL || numBytes < 1)
		return 0;

	const unsigned char* p = (const unsigned char*)bytes;
	uint32_t cp = p[0];
	int32 len = 1;
	if (cp < 0x80) {
		return cp;
	} else if ((cp & 0xe0) == 0xc0) {
		cp &= 0x1f;
		len = 2;
	} else if ((cp & 0xf0) == 0xe0) {
		cp &= 0x0f;
		len = 3;
	} else if ((cp & 0xf8) == 0xf0) {
		cp &= 0x07;
		len = 4;
	} else {
		return 0;
	}

	if (numBytes < len)
		return 0;

	for (int32 i = 1; i < len; i++) {
		if ((p[i] & 0xc0) != 0x80)
			return 0;
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	return cp;
}


uint32_t
EventAdapter::_LegacyModsToWPE(uint32 legacyMods)
{
	uint32_t wpe = 0;
	if (legacyMods & B_SHIFT_KEY)
		wpe |= wpe_input_keyboard_modifier_shift;
	// The V\OS default keymap puts Command on the physical Control key,
	// so browser shortcuts (Ctrl+C) arrive as B_COMMAND_KEY.
	if (legacyMods & (B_COMMAND_KEY | B_CONTROL_KEY))
		wpe |= wpe_input_keyboard_modifier_control;
	if (legacyMods & B_OPTION_KEY)
		wpe |= wpe_input_keyboard_modifier_alt;
	return wpe;
}


uint32_t
EventAdapter::_LegacyPointerModsToWPE(uint32 legacyMods)
{
	uint32_t wpe = _LegacyModsToWPE(legacyMods);
	if (legacyMods & B_CONTROL_KEY)
		wpe |= wpe_input_pointer_modifier_button2;
	return wpe;
}


uint32_t
EventAdapter::_LegacyButtonToWPE(int32 buttons)
{
	// WPE wants one button number: 1 primary, 2 secondary, 3 middle.
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		return 1;
	if (buttons & B_SECONDARY_MOUSE_BUTTON)
		return 2;
	if (buttons & B_TERTIARY_MOUSE_BUTTON)
		return 3;
	return 0;
}


uint32_t
EventAdapter::_LegacyKeyToXKB(int32 rawKey, const char* bytes, int32 numBytes)
{
	// Ctrl+letter arrives folded to 0x01-0x1a by xkb; expand it back so
	// the page sees the real letter key with the control modifier.
	uint32_t modifiers = 0;
	if (numBytes > 0 && bytes != NULL) {
		unsigned char b0 = (unsigned char)bytes[0];
		if (b0 >= 0x01 && b0 <= 0x1a && numBytes == 1)
			return wpe_unicode_to_key_code((uint32_t)('a' + b0 - 1));
	}

	// Printable characters, including multi-byte UTF-8 text: xkb is the
	// character authority on V\OS, so the decoded codepoint is right.
	if (numBytes > 0 && bytes != NULL) {
		unsigned char b0 = (unsigned char)bytes[0];
		if (b0 >= 0x20 && b0 < 0x7f)
			return wpe_unicode_to_key_code((uint32_t)b0);
		if (b0 >= 0x80) {
			uint32_t cp = _DecodeUTF8(bytes, numBytes);
			if (cp != 0)
				return wpe_unicode_to_key_code(cp);
		}
	}

	// Special keys, keyed by the legacy keycode.
	switch (rawKey) {
		case kLegacyEscKey:			return kXK_Escape;
		case kLegacyPrintKey:		return kXK_Print;
		case kLegacyScrollLockKey:	return kXK_Scroll_Lock;
		case kLegacyPauseKey:		return kXK_Pause;
		case B_F1_KEY:				return kXK_F1;
		case B_F2_KEY:				return kXK_F1 + 1;
		case B_F3_KEY:				return kXK_F1 + 2;
		case B_F4_KEY:				return kXK_F1 + 3;
		case B_F5_KEY:				return kXK_F1 + 4;
		case B_F6_KEY:				return kXK_F1 + 5;
		case B_F7_KEY:				return kXK_F1 + 6;
		case B_F8_KEY:				return kXK_F1 + 7;
		case B_F9_KEY:				return kXK_F1 + 8;
		case B_F10_KEY:				return kXK_F1 + 9;
		case B_F11_KEY:				return kXK_F1 + 10;
		case B_F12_KEY:				return kXK_F1 + 11;
		case kLegacyBackspaceKey:	return kXK_BackSpace;
		case kLegacyInsertKey:		return kXK_Insert;
		case kLegacyHomeKey:			return kXK_Home;
		case kLegacyPageUpKey:		return kXK_Page_Up;
		case kLegacyNumLockKey:		return kXK_Num_Lock;
		case kLegacyTabKey:			return kXK_Tab;
		case kLegacyDeleteKey:		return kXK_Delete;
		case kLegacyEndKey:			return kXK_End;
		case kLegacyPageDownKey:		return kXK_Page_Down;
		case kLegacyCapsLockKey:		return kXK_Caps_Lock;
		case kLegacyEnterKey:		return kXK_Return;
		case kLegacyLeftShiftKey:	return kXK_Shift_L;
		case kLegacyRightShiftKey:	return kXK_Shift_R;
		case kLegacyUpKey:			return kXK_Up;
		case kLegacyKPEnterKey:		return kXK_KP_Enter;
		case kLegacyLeftCtrlKey:		return kXK_Control_L;
		case kLegacyLeftAltKey:		return kXK_Alt_L;
		case kLegacyRightAltKey:		return kXK_Alt_R;
		case kLegacyRightCtrlKey:	return kXK_Control_R;
		case kLegacyLeftKey:			return kXK_Left;
		case kLegacyDownKey:			return kXK_Down;
		case kLegacyRightKey:		return kXK_Right;
		case kLegacyLeftMetaKey:		return kXK_Super_L;
		case kLegacyRightMetaKey:	return kXK_Super_R;
		case kLegacyMenuKey:			return kXK_Menu;
		default:
			break;
	}

	// Keypad and misc keys arrive as raw evdev codes (>=0x80, or legacy
	//-range values the legacy table does not cover).
	switch (rawKey) {
		case kEvdevPlayPause:	return kXK_XF86AudioPlay;
		case kEvdevStop:		return kXK_XF86AudioStop;
		case kEvdevNextTrack:	return kXK_XF86AudioNext;
		case kEvdevPrevTrack:	return kXK_XF86AudioPrev;
		case kEvdevMute:		return kXK_XF86AudioMute;
		case kEvdevVolumeDown:	return kXK_XF86AudioLower;
		case kEvdevVolumeUp:	return kXK_XF86AudioRaise;
		default:
			break;
	}

	(void)modifiers;
	(void)kXK_Escape_;
	(void)kXK_Space;
	return 0;
}


void
EventAdapter::_DispatchKey(uint32_t keyCode, uint32_t modifiers, bool pressed)
{
	if (keyCode == 0)
		return;

	if (pressed)
		fHeldKeys.insert(keyCode);
	else
		fHeldKeys.erase(keyCode);

	struct wpe_input_keyboard_event event;
	memset(&event, 0, sizeof(event));
	event.key_code = keyCode;
	event.modifiers = modifiers;
	event.pressed = pressed;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_keyboard_event);
}


//	#pragma mark - Public


void
EventAdapter::ReleaseAllKeys()
{
	for (uint32_t keyCode : fHeldKeys) {
		struct wpe_input_keyboard_event event;
		memset(&event, 0, sizeof(event));
		event.key_code = keyCode;
		event.pressed = false;
		dispatch_on_glib(fBackend, event,
			wpe_view_backend_dispatch_keyboard_event);
	}
	fHeldKeys.clear();
}


void
EventAdapter::TranslateKeyDown(const BMessage* msg)
{
	if (!fBackend)
		return;

	const char* bytes = nullptr;
	int32 numBytes = 0;
	msg->FindString("bytes", &bytes);
	if (bytes != nullptr)
		numBytes = strlen(bytes);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// B_KEY_DOWN/UP carry the raw key as int32; reading int8 always failed
	// and dropped every non-printable key (Enter, arrows, Backspace).
	int32 rawKey = 0;
	msg->FindInt32("key", &rawKey);

	// Raw chars back up the byte string for keys xkb folded or dropped.
	if (numBytes == 0) {
		int32 rawChar = 0;
		if (msg->FindInt32("raw_char", &rawChar) == B_OK
			&& rawChar >= 0x20 && rawChar < 0x7f) {
			static char sChar[2];
			sChar[0] = (char)rawChar;
			sChar[1] = 0;
			bytes = sChar;
			numBytes = 1;
		}
	}

	_DispatchKey(_LegacyKeyToXKB(rawKey, bytes, numBytes),
		_LegacyModsToWPE(modifiers), true);
}


void
EventAdapter::TranslateKeyUp(const BMessage* msg)
{
	if (!fBackend)
		return;

	const char* bytes = nullptr;
	int32 numBytes = 0;
	msg->FindString("bytes", &bytes);
	if (bytes != nullptr)
		numBytes = strlen(bytes);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	int32 rawKey = 0;
	msg->FindInt32("key", &rawKey);

	// Release the key we pressed even if the mapping changed in between
	// (layout switch while held): find it in the held set by the mapping
	// of this release, else release everything that went down.
	uint32_t keyCode = _LegacyKeyToXKB(rawKey, bytes, numBytes);
	if (keyCode != 0 && fHeldKeys.find(keyCode) != fHeldKeys.end()) {
		_DispatchKey(keyCode, _LegacyModsToWPE(modifiers), false);
		return;
	}

	// The release may not map (bytes already consumed): synthesize a
	// release for every held key so the page never sees a stuck key.
	if (keyCode == 0 && !fHeldKeys.empty()) {
		// Only when this looks like a real key release, not noise.
		if (rawKey != 0 || numBytes > 0)
			ReleaseAllKeys();
	}
}


void
EventAdapter::TranslateMouseDown(const BMessage* msg)
{
	if (!fBackend)
		return;

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

	int32 clicks = 1;
	msg->FindInt32("clicks", &clicks);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.type = wpe_input_pointer_event_type_button;
	event.x = (int32)where.x;
	event.y = (int32)where.y;
	event.button = _LegacyButtonToWPE(buttons);
	event.state = 1;
	fPressedButton = event.button;
	event.modifiers = _LegacyPointerModsToWPE(modifiers);

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseUp(const BMessage* msg)
{
	if (!fBackend)
		return;

	BPoint where;
	msg->FindPoint("where", &where);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.type = wpe_input_pointer_event_type_button;
	event.x = (int32)where.x;
	event.y = (int32)where.y;
	// The release message carries the buttons still held: report the
	// button that went down.
	event.button = fPressedButton;
	event.state = 0;
	fPressedButton = 0;
	event.modifiers = _LegacyPointerModsToWPE(modifiers);

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseMoved(const BMessage* msg)
{
	if (!fBackend)
		return;

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	uint32_t wpeMods = _LegacyPointerModsToWPE(modifiers);
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		wpeMods |= wpe_input_pointer_modifier_button1;
	else if (buttons & B_SECONDARY_MOUSE_BUTTON)
		wpeMods |= wpe_input_pointer_modifier_button2;
	else if (buttons & B_TERTIARY_MOUSE_BUTTON)
		wpeMods |= wpe_input_pointer_modifier_button3;

	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.type = wpe_input_pointer_event_type_motion;
	event.x = (int32)where.x;
	event.y = (int32)where.y;
	event.button = 0;
	event.state = (buttons != 0) ? 1 : 0;
	event.modifiers = wpeMods;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseWheel(const BMessage* msg)
{
	if (!fBackend)
		return;

	BPoint where;
	msg->FindPoint("where", &where);

	float deltaX = 0, deltaY = 0;
	msg->FindFloat("be:wheel_delta_x", &deltaX);
	msg->FindFloat("be:wheel_delta_y", &deltaY);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// Legacy wheel deltas are float notches; libwpe wants pixels. Send
	// one smooth-scroll axis event per axis: 15 pixels per notch matches
	// the discrete wheel step other WPE embedders use. WPE counts
	// positive values as scrolling up or left, the opposite sign of the
	// legacy deltas.
	const float deltas[2] = { deltaY, deltaX };
	for (uint32_t axis = 0; axis < 2; axis++) {
		if (deltas[axis] == 0.0f)
			continue;

		float pixels = -deltas[axis] * 15.0f;
		struct wpe_input_axis_event event;
		memset(&event, 0, sizeof(event));
		event.type = wpe_input_axis_event_type_motion_smooth;
		event.x = (int32)where.x;
		event.y = (int32)where.y;
		event.axis = axis;
		event.value = (int32)lroundf(pixels);
		if (event.value == 0)
			event.value = pixels > 0 ? 1 : -1;
		event.modifiers = _LegacyPointerModsToWPE(modifiers);

		dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_axis_event);
	}
}
