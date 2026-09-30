/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Input server filter that intercepts brightness hotkeys (Fn+Up/Down on
 * laptops) and adjusts screen brightness via the public BScreen API.
 *
 * This follows the same pattern as the switch_workspace filter: intercept
 * B_KEY_DOWN messages, then use BScreen::GetBrightness/SetBrightness.
 *
 * Multi-display policy (v1): always targets B_MAIN_SCREEN_ID (the default
 * BScreen() constructor).  A future revision could query the window under
 * the pointer to pick the display that owns the hotkey's logical position.
 */


#include "BrightnessHotkeyFilter.h"

#include <new>

#include <InterfaceDefs.h>
#include <Message.h>
#include <Screen.h>


// Evdev keycodes for brightness hotkeys (no Haiku equivalent in
// linux_to_haiku_keycode, so the raw evdev code passes through).
static const int32 kBrightnessDownKey = 224; // KEY_BRIGHTNESSDOWN
static const int32 kBrightnessUpKey = 225;   // KEY_BRIGHTNESSUP

static const float kBrightnessStep = 0.05f;


extern "C" BInputServerFilter* instantiate_input_filter() {
	return new(std::nothrow) BrightnessHotkeyFilter();
}


BrightnessHotkeyFilter::BrightnessHotkeyFilter()
{
}


filter_result
BrightnessHotkeyFilter::Filter(BMessage* message, BList* _list)
{
	if (message->what != B_KEY_DOWN && message->what != B_UNMAPPED_KEY_DOWN)
		return B_DISPATCH_MESSAGE;

	int32 key = 0;
	if (message->FindInt32("key", &key) != B_OK)
		return B_DISPATCH_MESSAGE;

	if (key != kBrightnessDownKey && key != kBrightnessUpKey)
		return B_DISPATCH_MESSAGE;

	// Use the public BScreen API for the main (primary) screen.
	// See multi-display policy note in the file header.
	BScreen screen(B_MAIN_SCREEN_ID);

	float currentBrightness;
	if (screen.GetBrightness(&currentBrightness) != B_OK)
		return B_SKIP_MESSAGE;

	// Calculate new brightness
	float newBrightness;
	if (key == kBrightnessUpKey)
		newBrightness = currentBrightness + kBrightnessStep;
	else
		newBrightness = currentBrightness - kBrightnessStep;

	// Clamp to [0, 1]
	if (newBrightness < 0.0f)
		newBrightness = 0.0f;
	else if (newBrightness > 1.0f)
		newBrightness = 1.0f;

	// Set new brightness
	screen.SetBrightness(newBrightness);

	return B_SKIP_MESSAGE;
}


status_t
BrightnessHotkeyFilter::InitCheck()
{
	return B_OK;
}
