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
#include <OS.h>
#include <Point.h>
#include <View.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <wpe/wpe.h>
#ifdef HAS_WPE_PLATFORM
#include <wpe/wpe-platform.h>
#endif


#ifdef HAS_WPE_PLATFORM
//	#pragma mark - WPEPlatform dispatch


struct KeyParams {
	uint32_t	keyval;
	uint32_t	keycode;
	uint32_t	modifiers;
	bool		pressed;
};


struct ButtonParams {
	double		x;
	double		y;
	uint32_t	button;
	uint32_t	modifiers;
	bool		pressed;
	bool		countPress;
	uint32_t	time;
};


struct MoveParams {
	double		x;
	double		y;
	double		deltaX;
	double		deltaY;
	uint32_t	modifiers;
	uint32_t	time;
};


struct ScrollParams {
	double		x;
	double		y;
	double		deltaX;
	double		deltaY;
	uint32_t	modifiers;
	uint32_t	time;
};


// Builds the typed WPEEvent on the GLib thread: press counting is
// stateful on the view, so it must run in dispatch order there.
template<typename Params>
static void
dispatch_on_glib(WPEView* view, Params* params,
	WPEEvent* (*make)(WPEView*, Params*))
{
	struct Call {
		WPEView*			view;
		Params*				params;
		WPEEvent*			(*make)(WPEView*, Params*);
	};
	auto* call = new Call{view, params, make};
	ProcessManager::Instance().InvokeOnGLibThread([](void* data) -> gboolean {
		auto* call = static_cast<Call*>(data);
		WPEEvent* event = call->make(call->view, call->params);
		if (event != nullptr) {
			wpe_view_event(call->view, event);
			wpe_event_unref(event);
		}
		delete call->params;
		delete call;
		return G_SOURCE_REMOVE;
	}, call);
}


static WPEEvent*
_make_key(WPEView* view, KeyParams* p)
{
	return wpe_event_keyboard_new(p->pressed
			? WPE_EVENT_KEYBOARD_KEY_DOWN : WPE_EVENT_KEYBOARD_KEY_UP,
		view, WPE_INPUT_SOURCE_KEYBOARD, 0,
		static_cast<WPEModifiers>(p->modifiers), p->keycode, p->keyval);
}


static WPEEvent*
_make_button(WPEView* view, ButtonParams* p)
{
	guint pressCount = 0;
	if (p->countPress && p->pressed) {
		pressCount = wpe_view_compute_press_count(view, p->x, p->y,
			p->button, p->time);
	}
	return wpe_event_pointer_button_new(p->pressed
			? WPE_EVENT_POINTER_DOWN : WPE_EVENT_POINTER_UP,
		view, WPE_INPUT_SOURCE_MOUSE, p->time,
		static_cast<WPEModifiers>(p->modifiers), p->button, p->x, p->y,
		pressCount);
}


static WPEEvent*
_make_move(WPEView* view, MoveParams* p)
{
	return wpe_event_pointer_move_new(WPE_EVENT_POINTER_MOVE, view,
		WPE_INPUT_SOURCE_MOUSE, p->time,
		static_cast<WPEModifiers>(p->modifiers), p->x, p->y,
		p->deltaX, p->deltaY);
}


static WPEEvent*
_make_scroll(WPEView* view, ScrollParams* p)
{
	return wpe_event_scroll_new(view, WPE_INPUT_SOURCE_MOUSE, p->time,
		static_cast<WPEModifiers>(p->modifiers), p->deltaX, p->deltaY,
		FALSE, FALSE, p->x, p->y);
}
#else
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
#endif


//	#pragma mark - Constructor/Destructor


#ifdef HAS_WPE_PLATFORM
EventAdapter::EventAdapter(WPEView* view)
	:
	fView(view),
	fPressedButton(0),
	fViewWidth(0),
	fViewHeight(0)
{
	log_info("EventAdapter: created (WPEPlatform)");
}
#else
EventAdapter::EventAdapter(struct wpe_view_backend* backend)
	:
	fBackend(backend),
	fPressedButton(0),
	fViewWidth(0),
	fViewHeight(0)
{
	log_info("EventAdapter: created");
}
#endif


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
		wpe |= WPE_MODIFIER_KEYBOARD_SHIFT;
	// V\OS puts Command on physical Control (Ctrl+C arrives as
	// B_COMMAND_KEY) and Control on physical Alt (Alt+Left).
	if (legacyMods & B_COMMAND_KEY)
		wpe |= WPE_MODIFIER_KEYBOARD_CONTROL;
	if (legacyMods & (B_CONTROL_KEY | B_OPTION_KEY))
		wpe |= WPE_MODIFIER_KEYBOARD_ALT;
	return wpe;
}


static uint32_t
_NowMs()
{
	// WebKit counts double clicks from the event timestamps.
	return (uint32_t)(system_time() / 1000);
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
#ifdef HAS_WPE_PLATFORM
	// WPEPlatform numbers buttons: 1 primary, 2 middle, 3 secondary.
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		return WPE_BUTTON_PRIMARY;
	if (buttons & B_TERTIARY_MOUSE_BUTTON)
		return WPE_BUTTON_MIDDLE;
	if (buttons & B_SECONDARY_MOUSE_BUTTON)
		return WPE_BUTTON_SECONDARY;
#else
	// The legacy libwpe input events number buttons: 1 primary,
	// 2 secondary, 3 middle.
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		return 1;
	if (buttons & B_SECONDARY_MOUSE_BUTTON)
		return 2;
	if (buttons & B_TERTIARY_MOUSE_BUTTON)
		return 3;
#endif
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


int32_t
EventAdapter::_Clamp(float v, int size)
{
	int32_t i = (int32_t)floorf(v);
	if (i < 0)
		return 0;
	if (size > 0 && i > size - 1)
		return size - 1;
	return i;
}


void
EventAdapter::SetViewSize(int width, int height)
{
	fViewWidth = width;
	fViewHeight = height;
}


#ifdef HAS_WPE_PLATFORM
void
EventAdapter::_DispatchKey(uint32_t keyval, uint32_t hardwareCode,
	uint32_t modifiers, bool pressed)
{
	if (keyval == 0 && hardwareCode == 0)
		return;

	uint64_t id = ((uint64_t)keyval << 32) | hardwareCode;
	if (pressed)
		fHeldKeys.insert(id);
	else
		fHeldKeys.erase(id);

	auto* params = new KeyParams{keyval, hardwareCode, modifiers, pressed};
	dispatch_on_glib(fView, params, _make_key);
}
#else
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
#endif


//	#pragma mark - Public


void
EventAdapter::ReleaseAllKeys()
{
#ifdef HAS_WPE_PLATFORM
	for (uint64_t id : fHeldKeys) {
		auto* params = new KeyParams{(uint32_t)(id >> 32),
			(uint32_t)(id & 0xffffffff), 0, false};
		dispatch_on_glib(fView, params, _make_key);
	}
	fHeldKeys.clear();
#else
	for (uint32_t keyCode : fHeldKeys) {
		struct wpe_input_keyboard_event event;
		memset(&event, 0, sizeof(event));
		event.key_code = keyCode;
		event.pressed = false;
		dispatch_on_glib(fBackend, event,
			wpe_view_backend_dispatch_keyboard_event);
	}
	fHeldKeys.clear();
#endif
}


void
EventAdapter::TranslateKeyDown(const BMessage* msg)
{
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL)
		return;
#else
	if (!fBackend)
		return;
#endif

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

	uint32_t keyval = _LegacyKeyToXKB(rawKey, bytes, numBytes, modifiers);
#ifdef HAS_WPE_PLATFORM
	// Hardware keycode for the DOM "code" property: evdev + 8.
	int32 evdev = web_legacy_key_to_evdev(rawKey);
	uint32_t keycode = evdev > 0 ? (uint32_t)(evdev + 8) : 0;
	_DispatchKey(keyval, keycode, _LegacyModsToWPE(modifiers), true);
#else
	_DispatchKey(keyval, _LegacyModsToWPE(modifiers), true);
#endif
}


void
EventAdapter::TranslateKeyUp(const BMessage* msg)
{
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL)
		return;
#else
	if (!fBackend)
		return;
#endif

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
#ifdef HAS_WPE_PLATFORM
	int32 evdev = web_legacy_key_to_evdev(rawKey);
	uint32_t hardwareCode = evdev > 0 ? (uint32_t)(evdev + 8) : 0;
	uint64_t id = ((uint64_t)keyCode << 32) | hardwareCode;
	if (keyCode != 0 && fHeldKeys.find(id) != fHeldKeys.end()) {
		_DispatchKey(keyCode, hardwareCode, _LegacyModsToWPE(modifiers),
			false);
		return;
	}

	// The release may not map (bytes already consumed): synthesize a
	// release for every held key so the page never sees a stuck key.
	if (keyCode == 0 && !fHeldKeys.empty()) {
		// Only when this looks like a real key release, not noise.
		if (rawKey != 0 || numBytes > 0)
			ReleaseAllKeys();
	}
#else
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
#endif
}


void
EventAdapter::TranslateMouseDown(const BMessage* msg)
{
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL)
		return;
#else
	if (!fBackend)
		return;
#endif

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	uint32_t button = _LegacyButtonToWPE(buttons);
	fPressedButton = button;
#ifdef HAS_WPE_PLATFORM
	uint32_t wpeMods = _LegacyPointerModsToWPE(modifiers);
	if (button != 0)
		wpeMods |= WPE_MODIFIER_POINTER_BUTTON1 << (button - 1);

	auto* params = new ButtonParams{(double)_Clamp(where.x, fViewWidth),
		(double)_Clamp(where.y, fViewHeight), button, wpeMods, true, true,
		_NowMs()};
	dispatch_on_glib(fView, params, _make_button);
#else
	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.time = _NowMs();
	event.type = wpe_input_pointer_event_type_button;
	event.x = _Clamp(where.x, fViewWidth);
	event.y = _Clamp(where.y, fViewHeight);
	event.button = button;
	event.state = 1;
	event.modifiers = _LegacyPointerModsToWPE(modifiers);

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
#endif
}


void
EventAdapter::TranslateMouseUp(const BMessage* msg)
{
	// No press reached the page (the context menu took it).
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL || fPressedButton == 0)
		return;
#else
	if (!fBackend || fPressedButton == 0)
		return;
#endif

	BPoint where;
	msg->FindPoint("where", &where);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

#ifdef HAS_WPE_PLATFORM
	// The release message carries the buttons still held: report the
	// button that went down, without its modifier bit.
	uint32_t wpeMods = _LegacyPointerModsToWPE(modifiers);
	uint32_t button = fPressedButton;
	fPressedButton = 0;

	auto* params = new ButtonParams{(double)_Clamp(where.x, fViewWidth),
		(double)_Clamp(where.y, fViewHeight), button, wpeMods, false, false,
		_NowMs()};
	dispatch_on_glib(fView, params, _make_button);
#else
	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.time = _NowMs();
	event.type = wpe_input_pointer_event_type_button;
	event.x = _Clamp(where.x, fViewWidth);
	event.y = _Clamp(where.y, fViewHeight);
	// The release message carries the buttons still held: report the
	// button that went down.
	event.button = fPressedButton;
	event.state = 0;
	fPressedButton = 0;
	event.modifiers = _LegacyPointerModsToWPE(modifiers);

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
#endif
}


void
EventAdapter::TranslateMouseMoved(const BMessage* msg)
{
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL)
		return;
#else
	if (!fBackend)
		return;
#endif

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// Moves during a drag may not carry the buttons; the press we sent
	// is still held until its release.
#ifdef HAS_WPE_PLATFORM
	if (buttons == 0 && fPressedButton == WPE_BUTTON_PRIMARY)
		buttons = B_PRIMARY_MOUSE_BUTTON;
	else if (buttons == 0 && fPressedButton == WPE_BUTTON_SECONDARY)
		buttons = B_SECONDARY_MOUSE_BUTTON;
	else if (buttons == 0 && fPressedButton == WPE_BUTTON_MIDDLE)
		buttons = B_TERTIARY_MOUSE_BUTTON;
#else
	if (buttons == 0 && fPressedButton == 1)
		buttons = B_PRIMARY_MOUSE_BUTTON;
	else if (buttons == 0 && fPressedButton == 2)
		buttons = B_SECONDARY_MOUSE_BUTTON;
	else if (buttons == 0 && fPressedButton == 3)
		buttons = B_TERTIARY_MOUSE_BUTTON;
#endif

	uint32_t wpeMods = _LegacyPointerModsToWPE(modifiers);
	if (buttons & B_PRIMARY_MOUSE_BUTTON)
		wpeMods |= WPE_MODIFIER_POINTER_BUTTON1;
	else if (buttons & B_SECONDARY_MOUSE_BUTTON)
		wpeMods |= WPE_MODIFIER_POINTER_BUTTON2;
	else if (buttons & B_TERTIARY_MOUSE_BUTTON)
		wpeMods |= WPE_MODIFIER_POINTER_BUTTON3;

#ifdef HAS_WPE_PLATFORM
	auto* params = new MoveParams{(double)_Clamp(where.x, fViewWidth),
		(double)_Clamp(where.y, fViewHeight), 0.0, 0.0, wpeMods, _NowMs()};
	dispatch_on_glib(fView, params, _make_move);
#else
	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.time = _NowMs();
	event.type = wpe_input_pointer_event_type_motion;
	event.x = _Clamp(where.x, fViewWidth);
	event.y = _Clamp(where.y, fViewHeight);
	event.button = 0;
	event.state = (buttons != 0) ? 1 : 0;
	event.modifiers = wpeMods;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
#endif
}


void
EventAdapter::TranslateMouseWheel(const BMessage* msg)
{
#ifdef HAS_WPE_PLATFORM
	if (fView == NULL)
		return;
#else
	if (!fBackend)
		return;
#endif

	BPoint where;
	msg->FindPoint("where", &where);

	float deltaX = 0, deltaY = 0;
	msg->FindFloat("be:wheel_delta_x", &deltaX);
	msg->FindFloat("be:wheel_delta_y", &deltaY);

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// Legacy wheel deltas are float notches; WPE wants pixels
	// (15 per notch, sign flipped).
	const float deltas[2] = { deltaY, deltaX };
#ifdef HAS_WPE_PLATFORM
	// The scroll event carries both axes at once.
	double pixelX = 0.0;
	double pixelY = 0.0;
	if (deltas[0] != 0.0f)
		pixelY = -(double)(deltas[0] * 15.0f);
	if (deltas[1] != 0.0f)
		pixelX = -(double)(deltas[1] * 15.0f);
	if (pixelX == 0.0 && pixelY == 0.0)
		return;

	auto* params = new ScrollParams{(double)_Clamp(where.x, fViewWidth),
		(double)_Clamp(where.y, fViewHeight), pixelX, pixelY,
		_LegacyPointerModsToWPE(modifiers), _NowMs()};
	dispatch_on_glib(fView, params, _make_scroll);
#else
	for (uint32_t axis = 0; axis < 2; axis++) {
		if (deltas[axis] == 0.0f)
			continue;

		float pixels = -deltas[axis] * 15.0f;
		struct wpe_input_axis_event event;
		memset(&event, 0, sizeof(event));
		event.type = wpe_input_axis_event_type_motion_smooth;
		event.x = _Clamp(where.x, fViewWidth);
		event.y = _Clamp(where.y, fViewHeight);
		event.axis = axis;
		event.value = (int32)lroundf(pixels);
		if (event.value == 0)
			event.value = pixels > 0 ? 1 : -1;
		event.modifiers = _LegacyPointerModsToWPE(modifiers);

		dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_axis_event);
	}
#endif
}
