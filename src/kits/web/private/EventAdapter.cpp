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
}


//	#pragma mark - Private


// V\OS hybrid keycode space: <=0x7f is the legacy keycode, not an
// evdev code and not the ASCII byte constants in InterfaceDefs.h.
// Values match the input_server mapping (see LinuxKeycodeMap.h).
enum {
	kLegacyEscKey		= 0x01,
	kLegacyBackspaceKey	= 0x1e,
	kLegacyInsertKey		= 0x1f,
	kLegacyHomeKey		= 0x20,
	kLegacyPageUpKey		= 0x21,
	kLegacyTabKey		= 0x26,
	kLegacyDeleteKey		= 0x34,
	kLegacyEndKey		= 0x35,
	kLegacyPageDownKey	= 0x36,
	kLegacyEnterKey		= 0x47,
	kLegacyUpKey		= 0x57,
	kLegacyLeftKey		= 0x61,
	kLegacyDownKey		= 0x62,
	kLegacyRightKey		= 0x63
};


uint32_t
EventAdapter::_LegacyModsToWPE(uint32 legacyMods)
{
	uint32_t wpe = 0;
	if (legacyMods & B_SHIFT_KEY)    wpe |= wpe_input_keyboard_modifier_shift;
	if (legacyMods & B_CONTROL_KEY)  wpe |= wpe_input_keyboard_modifier_control;
	if (legacyMods & B_COMMAND_KEY)  wpe |= wpe_input_keyboard_modifier_control;
	if (legacyMods & B_OPTION_KEY)   wpe |= wpe_input_keyboard_modifier_alt;
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
EventAdapter::_LegacyKeyToXKB(uint8 rawKey, const char* bytes, int32 numBytes)
{
	if (numBytes > 0 && bytes[0] >= 0x20 && bytes[0] < 0x7f)
		return wpe_unicode_to_key_code((uint32_t)(unsigned char)bytes[0]);

	switch (rawKey) {
		case kLegacyEscKey:       return 0xff1b;
		case B_F1_KEY:           return 0xffbe;
		case B_F2_KEY:           return 0xffbf;
		case B_F3_KEY:           return 0xffc0;
		case B_F4_KEY:           return 0xffc1;
		case B_F5_KEY:           return 0xffc2;
		case B_F6_KEY:           return 0xffc3;
		case B_F7_KEY:           return 0xffc4;
		case B_F8_KEY:           return 0xffc5;
		case B_F9_KEY:           return 0xffc6;
		case B_F10_KEY:          return 0xffc7;
		case B_F11_KEY:          return 0xffc8;
		case B_F12_KEY:          return 0xffc9;
		case kLegacyBackspaceKey: return 0xff08;
		case kLegacyInsertKey:    return 0xff63;
		case kLegacyHomeKey:      return 0xff50;
		case kLegacyPageUpKey:    return 0xff55;
		case kLegacyTabKey:       return 0xff09;
		case kLegacyDeleteKey:    return 0xffff;
		case kLegacyEndKey:       return 0xff57;
		case kLegacyPageDownKey:  return 0xff56;
		case kLegacyEnterKey:     return 0xff0d;
		case kLegacyUpKey:        return 0xff52;
		case kLegacyLeftKey:      return 0xff51;
		case kLegacyDownKey:      return 0xff54;
		case kLegacyRightKey:     return 0xff53;
		default:                 return 0;
	}
}


//	#pragma mark - Public


void
EventAdapter::TranslateKeyDown(const BMessage* msg)
{
	if (!fBackend) return;

	const char* bytes = nullptr;
	int32 numBytes = 0;
	msg->FindString("bytes", &bytes);
	if (!bytes) {
		bytes = "";
		numBytes = 0;
	} else {
		numBytes = strlen(bytes);
	}

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// B_KEY_DOWN/UP carry the raw key as int32; reading int8 always failed
	// and dropped every non-printable key (Enter, arrows, Backspace).
	int32 rawKey = 0;
	msg->FindInt32("key", &rawKey);

	uint32_t keyCode = _LegacyKeyToXKB(rawKey, bytes, numBytes);
	uint32_t wpeMods = _LegacyModsToWPE(modifiers);

	struct wpe_input_keyboard_event event;
	memset(&event, 0, sizeof(event));
	event.key_code = keyCode;
	event.modifiers = wpeMods;
	event.pressed = true;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_keyboard_event);
}


void
EventAdapter::TranslateKeyUp(const BMessage* msg)
{
	if (!fBackend) return;

	const char* bytes = nullptr;
	int32 numBytes = 0;
	msg->FindString("bytes", &bytes);
	if (!bytes) {
		bytes = "";
		numBytes = 0;
	} else {
		numBytes = strlen(bytes);
	}

	uint32 modifiers = 0;
	msg->FindInt32("modifiers", (int32*)&modifiers);

	// B_KEY_DOWN/UP carry the raw key as int32; reading int8 always failed
	// and dropped every non-printable key (Enter, arrows, Backspace).
	int32 rawKey = 0;
	msg->FindInt32("key", &rawKey);

	uint32_t keyCode = _LegacyKeyToXKB(rawKey, bytes, numBytes);
	uint32_t wpeMods = _LegacyModsToWPE(modifiers);

	struct wpe_input_keyboard_event event;
	memset(&event, 0, sizeof(event));
	event.key_code = keyCode;
	event.modifiers = wpeMods;
	event.pressed = false;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_keyboard_event);
}


void
EventAdapter::TranslateMouseDown(const BMessage* msg)
{
	if (!fBackend) return;

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

	int32 clicks = 1;
	msg->FindInt32("clicks", &clicks);

	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.type = wpe_input_pointer_event_type_button;
	event.x = (int32)where.x;
	event.y = (int32)where.y;
	event.button = _LegacyButtonToWPE(buttons);
	event.state = 1;
	fPressedButton = event.button;
	event.modifiers = 0;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseUp(const BMessage* msg)
{
	if (!fBackend) return;

	BPoint where;
	msg->FindPoint("where", &where);

	int32 buttons = 0;
	msg->FindInt32("buttons", &buttons);

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
	event.modifiers = 0;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseMoved(const BMessage* msg)
{
	if (!fBackend) return;

	BPoint where;
	msg->FindPoint("where", &where);

	struct wpe_input_pointer_event event;
	memset(&event, 0, sizeof(event));
	event.type = wpe_input_pointer_event_type_motion;
	event.x = (int32)where.x;
	event.y = (int32)where.y;
	event.button = 0;
	event.state = 0;
	event.modifiers = 0;

	dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_pointer_event);
}


void
EventAdapter::TranslateMouseWheel(const BMessage* msg)
{
	if (!fBackend) return;

	BPoint where;
	msg->FindPoint("where", &where);

	float deltaX = 0, deltaY = 0;
	msg->FindFloat("be:wheel_delta_x", &deltaX);
	msg->FindFloat("be:wheel_delta_y", &deltaY);

	// One WPE axis event per wheel axis; WPE counts positive values as
	// scrolling up or left, the opposite of the wheel deltas.
	const float deltas[2] = { deltaY, deltaX };
	for (uint32_t axis = 0; axis < 2; axis++) {
		if (deltas[axis] == 0)
			continue;
		struct wpe_input_axis_event event;
		memset(&event, 0, sizeof(event));
		event.type = wpe_input_axis_event_type_motion;
		event.x = (int32)where.x;
		event.y = (int32)where.y;
		event.axis = axis;
		event.value = deltas[axis] > 0 ? -1 : 1;

		int32 steps = (int32)(deltas[axis] > 0 ? deltas[axis] : -deltas[axis]);
		if (steps < 1)
			steps = 1;
		for (int32 i = 0; i < steps; i++)
			dispatch_on_glib(fBackend, event, wpe_view_backend_dispatch_axis_event);
	}
}
