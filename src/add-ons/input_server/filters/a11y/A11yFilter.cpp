/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * input_server filter for V\OS accessibility input aids.
 */


#include "A11yFilter.h"
#include "A11yFilterLogic.h"

#include <new>
#include <string.h>

#include <InterfaceDefs.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <Screen.h>
#include <View.h>
#include <WindowScreen.h>

#include "AccessibilitySettings.h"


extern "C" BInputServerFilter* instantiate_input_filter() {
	return new(std::nothrow) A11yFilter();
}


static const bigtime_t kSettingsReloadInterval = 500000;
static const bigtime_t kStickyTapWindow = 400000;


// Sticky keys only latch the real modifiers, not Caps/Num/Scroll.
static uint32
sticky_eligible_modifier(uint32 modBit)
{
	if (modBit == 0)
		return 0;
	if ((modBit & (B_SHIFT_KEY | B_CONTROL_KEY | B_COMMAND_KEY
			| B_OPTION_KEY)) == 0)
		return 0;
	return modBit;
}


A11yFilter::A11yFilter()
	:
	fSettings(kDefaultAccessibilitySettings),
	fSettingsTime(-1),
	fStickyTapCount(0),
	fLastStickyTap(0),
	fHasPendingKey(false),
	fPendingKeyKey(0),
	fPendingKeySince(0),
	fLatchedModifiers(0),
	fLockedModifiers(0),
	fStickyAppliedKey(0),
	fStickyAppliedMods(0)
{
	_ReloadSettings(true);
}


filter_result
A11yFilter::Filter(BMessage* message, BList* list)
{
	_ReloadSettings(false);

	// Slow keys off while a key is still stashed: drop the stash.
	if (!fSettings.slow_keys && fHasPendingKey) {
		fHasPendingKey = false;
		fPendingKey.MakeEmpty();
	}

	int32 key = 0;
	bool hasKey = message->FindInt32("key", &key) == B_OK;
	uint32 keyModifiers = 0;
	if (message->FindInt32("modifiers", (int32*)&keyModifiers) != B_OK)
		keyModifiers = 0;

	// Mouse keys also consume the keypad key-up so the key never
	// types a digit when the pointer is being driven.
	if (fSettings.mouse_keys && message->what == B_KEY_UP && hasKey
		&& _SwallowMouseKeyUp(key, keyModifiers))
		return B_SKIP_MESSAGE;

	// Immediate actions run before slow-keys gating so shortcuts and
	// mouse keys stay responsive while another key is still pending.
	if (hasKey && (message->what == B_KEY_DOWN
			|| message->what == B_UNMAPPED_KEY_DOWN)) {
		if (_HandleGlobalShortcut(key, keyModifiers))
			return B_SKIP_MESSAGE;

		filter_result mouse = _HandleMouseKeys(key, keyModifiers, message,
			list);
		// A filled out-list means mouse keys took the key.
		if (mouse != B_DISPATCH_MESSAGE || (list != NULL && !list->IsEmpty()))
			return mouse;

		if (fSettings.sticky_keys) {
			if (fKeymap.IsModifierKey(key))
				return _HandleStickyTap(key);
			return _ApplyStickyModifiers(message, list);
		}
	}

	// Sticky modifiers still apply to the key-up of the key that used them.
	if (fSettings.sticky_keys && message->what == B_KEY_UP && hasKey
		&& key == fStickyAppliedKey && fStickyAppliedMods != 0) {
		int32 current = 0;
		message->FindInt32("modifiers", &current);
		message->ReplaceInt32("modifiers",
			(int32)(current | fStickyAppliedMods));
		fStickyAppliedKey = 0;
		fStickyAppliedMods = 0;
	}

	// Slow keys: stash keydowns, decide on the next event.
	if (fSettings.slow_keys) {
		if (message->what == B_KEY_UP && hasKey)
			return _SlowKeysOnKeyUp(message, key, list);
		if ((message->what == B_KEY_DOWN || message->what == B_UNMAPPED_KEY_DOWN)
				&& hasKey)
			return _SlowKeysOnKeyDown(message, key);
		if (fHasPendingKey)
			return _SlowKeysOnOther(message, list);
	}

	return B_DISPATCH_MESSAGE;
}


status_t
A11yFilter::InitCheck()
{
	return B_OK;
}


void
A11yFilter::_ReloadSettings(bool force)
{
	bigtime_t now = system_time();
	if (!force && fSettingsTime >= 0
		&& now - fSettingsTime < kSettingsReloadInterval)
		return;
	fSettingsTime = now;
	accessibility_load_settings(fSettings);
	// Modifier roles live in the keymap; reload it with the settings.
	fKeymap.SetToCurrent();
}


bool
A11yFilter::_HandleGlobalShortcut(int32 key, uint32 modifiers)
{
	// General bits, so either modifier mode matches physical Ctrl+Alt+Shift.
	const uint32 kCombo = B_SHIFT_KEY | B_CONTROL_KEY | B_COMMAND_KEY;

	a11y_global_action action = a11y_global_action_for(key, modifiers, kCombo);
	if (action == kA11yGlobalNone)
		return false;

	if (action == kA11yGlobalToggleSticky)
		fSettings.sticky_keys = !fSettings.sticky_keys;
	else if (action == kA11yGlobalToggleSlow) {
		fSettings.slow_keys = !fSettings.slow_keys;
		if (!fSettings.slow_keys) {
			fHasPendingKey = false;
			fPendingKey.MakeEmpty();
		}
	} else
		fSettings.mouse_keys = !fSettings.mouse_keys;

	accessibility_save_settings(fSettings);
	return true;
}


filter_result
A11yFilter::_HandleStickyTap(int32 key)
{
	uint32 modBit = sticky_eligible_modifier(fKeymap.Modifier(key));
	if (modBit == 0)
		return B_DISPATCH_MESSAGE;

	// A tap on an already-locked modifier unlocks it.
	if ((fLockedModifiers & modBit) != 0) {
		fLockedModifiers &= ~modBit;
		return B_DISPATCH_MESSAGE;
	}

	bigtime_t now = system_time();
	if (fStickyTapCount > 0 && now - fLastStickyTap > kStickyTapWindow)
		fStickyTapCount = 0;

	fStickyTapCount++;
	fLastStickyTap = now;

	a11y_sticky_tap_action action = a11y_sticky_tap_action_for(fStickyTapCount);
	if (action == kA11yStickyTapLatch) {
		fLatchedModifiers |= modBit;
	} else if (action == kA11yStickyTapLock) {
		fLockedModifiers |= modBit;
		fLatchedModifiers &= ~modBit;
	} else {
		// Triple tap toggles the feature; clear latch and lock state.
		fSettings.sticky_keys = !fSettings.sticky_keys;
		accessibility_save_settings(fSettings);
		fStickyTapCount = 0;
		fLatchedModifiers = 0;
		fLockedModifiers = 0;
	}
	// Let the modifier tap itself dispatch; the effect lands on the
	// next non-modifier key.
	return B_DISPATCH_MESSAGE;
}


filter_result
A11yFilter::_ApplyStickyModifiers(BMessage* message, BList* list)
{
	uint32 latched = fLatchedModifiers;
	uint32 locked = fLockedModifiers;
	if (latched == 0 && locked == 0)
		return B_DISPATCH_MESSAGE;

	uint32 sticky = latched | locked;
	int32 current = 0;
	message->FindInt32("modifiers", &current);
	uint32 newMods = current | sticky;
	message->ReplaceInt32("modifiers", (int32)newMods);

	// The add-on already filled bytes/byte from the unmodified keymap;
	// ORing modifiers alone would still type the base character.
	int32 key = 0;
	if (message->FindInt32("key", &key) == B_OK && key != 0) {
		char* chars = NULL;
		int32 numBytes = 0;
		fKeymap.GetChars(key, newMods, 0, &chars, &numBytes);
		if (chars != NULL && numBytes > 0) {
			message->RemoveName("bytes");
			message->RemoveName("byte");
			for (int32 i = 0; i < numBytes; i++)
				message->AddInt8("byte", (int8)chars[i]);
			message->AddData("bytes", B_STRING_TYPE, chars, numBytes + 1);
			// raw_char stays: by definition the unmodified character.
		} else {
			// Nothing maps under the sticky modifiers: unmapped, like
			// the keyboard add-on.
			message->what = message->what == B_KEY_UP
				? B_UNMAPPED_KEY_UP : B_UNMAPPED_KEY_DOWN;
		}
		delete[] chars;

		fStickyAppliedKey = key;
		fStickyAppliedMods = sticky;
	}

	fLatchedModifiers = 0;
	return B_DISPATCH_MESSAGE;
}


filter_result
A11yFilter::_HandleMouseKeys(int32 key, uint32 modifiers, BMessage* message,
	BList* list)
{
	if (!fSettings.mouse_keys)
		return B_DISPATCH_MESSAGE;

	a11y_mouse_key_action action = a11y_mouse_key_action_for(key);
	if (action == kA11yMouseKeyNone)
		return B_DISPATCH_MESSAGE;

	// NumLock on means the keypad types; leave those events alone.
	if ((modifiers & B_NUM_LOCK) != 0)
		return B_DISPATCH_MESSAGE;

	float stepTable[] = { 1.0f, 4.0f, 10.0f, 25.0f, 50.0f };
	int32 speed = fSettings.mouse_keys_speed;
	if (speed < 0)
		speed = 0;
	if (speed > 4)
		speed = 4;
	float step = stepTable[speed];

	BPoint where;
	uint32 buttons = 0;
	if (get_mouse(&where, &buttons) != B_OK)
		return B_DISPATCH_MESSAGE;

	BScreen screen(B_MAIN_SCREEN_ID);
	BRect frame = screen.Frame();

	int32 clicks = 0;
	switch (action) {
		case kA11yMouseKeyLeft:
			where.x -= step;
			break;
		case kA11yMouseKeyRight:
			where.x += step;
			break;
		case kA11yMouseKeyUp:
			where.y -= step;
			break;
		case kA11yMouseKeyDown:
			where.y += step;
			break;
		case kA11yMouseKeyUpLeft:
			where.x -= step;
			where.y -= step;
			break;
		case kA11yMouseKeyUpRight:
			where.x += step;
			where.y -= step;
			break;
		case kA11yMouseKeyDownLeft:
			where.x -= step;
			where.y += step;
			break;
		case kA11yMouseKeyDownRight:
			where.x += step;
			where.y += step;
			break;
		case kA11yMouseKeyClick:
		{
			// KP5 click toggles the primary button.
			where.ConstrainTo(frame);
			bigtime_t when = system_time();
			BMessage* down = new BMessage(B_MOUSE_DOWN);
			down->AddInt64("when", when);
			down->AddPoint("where", where);
			down->AddInt32("buttons", B_PRIMARY_MOUSE_BUTTON);
			down->AddInt32("clicks", ++clicks);
			down->AddInt32("modifiers", modifiers);
			BMessage* up = new BMessage(B_MOUSE_UP);
			up->AddInt64("when", when + 1);
			up->AddPoint("where", where);
			up->AddInt32("buttons", 0);
			up->AddInt32("clicks", clicks);
			up->AddInt32("modifiers", modifiers);
			if (list != NULL) {
				list->AddItem(down);
				list->AddItem(up);
			} else {
				delete down;
				delete up;
			}
			// input_server drops a skipped event's out-list; dispatch replaces it.
			return list != NULL && !list->IsEmpty()
				? B_DISPATCH_MESSAGE : B_SKIP_MESSAGE;
		}
		case kA11yMouseKeyConsume:
			return B_SKIP_MESSAGE;
		default:
			return B_DISPATCH_MESSAGE;
	}

	where.ConstrainTo(frame);

	// Inject B_MOUSE_MOVED like the other input_server filters do instead
	// of calling set_mouse_position from inside the server.
	if (list != NULL) {
		BMessage* moved = new BMessage(B_MOUSE_MOVED);
		moved->AddInt64("when", system_time());
		moved->AddPoint("where", where);
		moved->AddInt32("buttons", buttons);
		moved->AddInt32("clicks", clicks);
		moved->AddInt32("modifiers", modifiers);
		list->AddItem(moved);
	} else
		set_mouse_position((int32)where.x, (int32)where.y);

	return list != NULL && !list->IsEmpty()
		? B_DISPATCH_MESSAGE : B_SKIP_MESSAGE;
}


bool
A11yFilter::_SwallowMouseKeyUp(int32 key, uint32 modifiers)
{
	if ((modifiers & B_NUM_LOCK) != 0)
		return false;
	return a11y_mouse_key_action_for(key) != kA11yMouseKeyNone;
}


filter_result
A11yFilter::_SlowKeysOnKeyDown(BMessage* message, int32 key)
{
	// Key repeat while pending: still holding, still waiting.
	if (fHasPendingKey && key == fPendingKeyKey)
		return B_SKIP_MESSAGE;

	// A different key arriving before the delay rejects the pending key.
	if (fHasPendingKey)
		fHasPendingKey = false;

	fPendingKey = *message;
	fPendingKeyKey = key;
	fPendingKeySince = system_time();
	fHasPendingKey = true;
	// Defer acceptance until the key has been held long enough.
	return B_SKIP_MESSAGE;
}


filter_result
A11yFilter::_SlowKeysOnOther(BMessage* message, BList* list)
{
	if (!fHasPendingKey)
		return B_DISPATCH_MESSAGE;

	bigtime_t now = system_time();
	if (!a11y_slow_key_accept(now, fPendingKeySince,
			(int64)(fSettings.slow_keys_delay * 1000000.0f)))
		return B_DISPATCH_MESSAGE;

	// Deliver the stashed keydown, then keep the current event.
	_FlushPendingSlowKey(list);
	if (list != NULL)
		list->AddItem(new BMessage(*message));
	return B_DISPATCH_MESSAGE;
}


filter_result
A11yFilter::_SlowKeysOnKeyUp(BMessage* message, int32 key, BList* list)
{
	if (!fHasPendingKey || key != fPendingKeyKey)
		return _SlowKeysOnOther(message, list);

	bigtime_t now = system_time();
	if (a11y_slow_key_accept(now, fPendingKeySince,
			(int64)(fSettings.slow_keys_delay * 1000000.0f))) {
		// Held long enough: deliver stashed keydown, then the keyup.
		_FlushPendingSlowKey(list);
		if (list != NULL)
			list->AddItem(new BMessage(*message));
		return B_DISPATCH_MESSAGE;
	}

	// Released before the delay: drop the stashed keydown entirely.
	fHasPendingKey = false;
	fPendingKey.MakeEmpty();
	return B_SKIP_MESSAGE;
}


void
A11yFilter::_FlushPendingSlowKey(BList* list)
{
	if (!fHasPendingKey)
		return;
	fHasPendingKey = false;
	if (list != NULL)
		list->AddItem(new BMessage(fPendingKey));
	fPendingKey.MakeEmpty();
}
