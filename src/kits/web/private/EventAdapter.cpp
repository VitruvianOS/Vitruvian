/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */

#include "event_adapter.h"

#include "KeyTables.h"
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


uint32_t
EventAdapter::_LegacyModsToWPE(uint32 legacyMods)
{
	uint32_t wpe = 0;
	if (legacyMods & B_SHIFT_KEY)
		wpe |= wpe_input_keyboard_modifier_shift;
	// V\OS puts Command on physical Control (Ctrl+C arrives as
	// B_COMMAND_KEY) and Control on physical Alt (Alt+Left).
	if (legacyMods & B_COMMAND_KEY)
		wpe |= wpe_input_keyboard_modifier_control;
	if (legacyMods & (B_CONTROL_KEY | B_OPTION_KEY))
		wpe |= wpe_input_keyboard_modifier_alt;
	return wpe;
}


uint32_t
EventAdapter::_LegacyPointerModsToWPE(uint32 legacyMods)
{
	// WebKit derives Ctrl+click (open in new window) from the control
	// modifier alone; button2 would mean the middle button is down.
	return _LegacyModsToWPE(legacyMods);
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
EventAdapter::_LegacyKeyToXKB(int32 rawKey, const char* bytes, int32 numBytes,
	uint32 modifiers)
{
	// Special keys first: Enter, Tab and Backspace also arrive as 0x0a,
	// 0x09 and 0x08, which would unfold to j, i and h below.
	uint32_t special = web_special_key_to_keysym(rawKey);
	if (special != 0)
		return special;

	// Ctrl+letter arrives folded to 0x01-0x1a by xkb; expand it back so
	// the page sees the real letter key with the control modifier.
	if ((modifiers & B_CONTROL_KEY) != 0 && numBytes == 1 && bytes != NULL) {
		uint32_t letter = web_unfold_ctrl_letter((unsigned char)bytes[0]);
		if (letter != 0)
			return wpe_unicode_to_key_code(letter);
	}

	// Printable characters, including multi-byte UTF-8 text: xkb is the
	// character authority on V\OS, so the decoded codepoint is right.
	if (numBytes > 0 && bytes != NULL) {
		unsigned char b0 = (unsigned char)bytes[0];
		if (b0 >= 0x20 && b0 < 0x7f)
			return wpe_unicode_to_key_code((uint32_t)b0);
		if (b0 >= 0x80) {
			uint32_t cp = web_decode_utf8(bytes, numBytes);
			if (cp != 0)
				return wpe_unicode_to_key_code(cp);
		}
	}

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

	_DispatchKey(_LegacyKeyToXKB(rawKey, bytes, numBytes, modifiers),
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
	uint32_t keyCode = _LegacyKeyToXKB(rawKey, bytes, numBytes, modifiers);
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

	// Legacy wheel deltas are float notches; libwpe wants pixels
	// (15 per notch, sign flipped).
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
