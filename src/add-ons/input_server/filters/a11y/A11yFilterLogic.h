/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Pure key-decision logic for the V\OS accessibility input filter.
 * No BeOS input-server dependencies so host tests can compile it alone.
 */
#ifndef A11Y_FILTER_LOGIC_H
#define A11Y_FILTER_LOGIC_H


#include <stdint.h>


enum a11y_mouse_key_action {
	kA11yMouseKeyNone = 0,
	kA11yMouseKeyLeft,
	kA11yMouseKeyRight,
	kA11yMouseKeyUp,
	kA11yMouseKeyDown,
	kA11yMouseKeyUpLeft,
	kA11yMouseKeyUpRight,
	kA11yMouseKeyDownLeft,
	kA11yMouseKeyDownRight,
	kA11yMouseKeyClick,
	kA11yMouseKeyConsume
};


enum a11y_sticky_tap_action {
	kA11yStickyTapIgnore = 0,
	kA11yStickyTapLatch,
	kA11yStickyTapLock,
	kA11yStickyTapToggleFeature
};


enum a11y_global_action {
	kA11yGlobalNone = 0,
	kA11yGlobalToggleSticky = 1,
	kA11yGlobalToggleSlow = 2,
	kA11yGlobalToggleMouse = 3
};


// Legacy keycodes, the space B_KEY_DOWN/B_KEY_UP "key" carries.
static const int32_t kA11yKeypad7 = 0x37;
static const int32_t kA11yKeypad8 = 0x38;
static const int32_t kA11yKeypad9 = 0x39;
static const int32_t kA11yKeypad4 = 0x48;
static const int32_t kA11yKeypad5 = 0x49;
static const int32_t kA11yKeypad6 = 0x4a;
static const int32_t kA11yKeypad1 = 0x58;
static const int32_t kA11yKeypad2 = 0x59;
static const int32_t kA11yKeypad3 = 0x5a;
static const int32_t kA11yKeypad0 = 0x64;
static const int32_t kA11yKeypadPeriod = 0x65;
static const int32_t kA11yKeypadEnter = 0x5b;

static const int32_t kA11yDigit1 = 0x12;
static const int32_t kA11yDigit2 = 0x13;
static const int32_t kA11yDigit3 = 0x14;


inline a11y_mouse_key_action
a11y_mouse_key_action_for(int32_t key)
{
	if (key == kA11yKeypad7)
		return kA11yMouseKeyUpLeft;
	if (key == kA11yKeypad8)
		return kA11yMouseKeyUp;
	if (key == kA11yKeypad9)
		return kA11yMouseKeyUpRight;
	if (key == kA11yKeypad4)
		return kA11yMouseKeyLeft;
	if (key == kA11yKeypad5)
		return kA11yMouseKeyClick;
	if (key == kA11yKeypad6)
		return kA11yMouseKeyRight;
	if (key == kA11yKeypad1)
		return kA11yMouseKeyDownLeft;
	if (key == kA11yKeypad2)
		return kA11yMouseKeyDown;
	if (key == kA11yKeypad3)
		return kA11yMouseKeyDownRight;
	if (key == kA11yKeypad0 || key == kA11yKeypadPeriod
		|| key == kA11yKeypadEnter)
		return kA11yMouseKeyConsume;
	return kA11yMouseKeyNone;
}


// Slow keys: accept a stashed keydown only once the hold outlasts the
// delay; drop it when released earlier.
inline bool
a11y_slow_key_accept(int64_t now, int64_t since, int64_t delayMicros)
{
	return (now - since) >= delayMicros;
}


// Sticky keys: classify a modifier tap by how many taps landed in the
// window. One tap latches, two lock, three toggle the feature off.
inline a11y_sticky_tap_action
a11y_sticky_tap_action_for(int32_t tapCount)
{
	if (tapCount <= 0)
		return kA11yStickyTapIgnore;
	if (tapCount == 1)
		return kA11yStickyTapLatch;
	if (tapCount == 2)
		return kA11yStickyTapLock;
	return kA11yStickyTapToggleFeature;
}


// Global shortcut: Ctrl+Alt+Shift+1/2/3 toggles sticky/slow/mouse keys.
// Keys are legacy digit keycodes; requiredCombo is the general modifier
// bits the caller wants (B_SHIFT_KEY | B_CONTROL_KEY | B_COMMAND_KEY).
inline a11y_global_action
a11y_global_action_for(int32_t key, uint32_t modifiers, uint32_t requiredCombo)
{
	if ((modifiers & requiredCombo) != requiredCombo)
		return kA11yGlobalNone;

	if (key == kA11yDigit1)
		return kA11yGlobalToggleSticky;
	if (key == kA11yDigit2)
		return kA11yGlobalToggleSlow;
	if (key == kA11yDigit3)
		return kA11yGlobalToggleMouse;
	return kA11yGlobalNone;
}


#endif	// A11Y_FILTER_LOGIC_H
