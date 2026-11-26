/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ACCESSIBILITY_WINDOW_H
#define ACCESSIBILITY_WINDOW_H


#include <Window.h>


class BButton;
class AccessibilityView;


// Posted by AccessibilityView after any settings change so Defaults/Revert
// can refresh.
const uint32 kAccessibilitySettingsChanged = 'a11U';


class AccessibilityWindow : public BWindow {
public:
								AccessibilityWindow();
	virtual						~AccessibilityWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateButtons();

			AccessibilityView*	fView;
			BButton*			fDefaultsButton;
			BButton*			fRevertButton;
};


#endif	// ACCESSIBILITY_WINDOW_H
