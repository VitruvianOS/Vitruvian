/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ACCESSIBILITY_VIEW_H
#define ACCESSIBILITY_VIEW_H


#include <GroupView.h>
#include <Message.h>

#include "AccessibilitySettings.h"


class BCheckBox;
class BMenuField;
class BButton;
class BMenuItem;
class BSlider;
class BStringView;
class BTextControl;


class AccessibilityView : public BGroupView {
public:
								AccessibilityView();
	virtual						~AccessibilityView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				SetDefaults();
			void				Revert();
			bool				IsDefaultable() const;
			bool				IsRevertable() const;

private:
			void				_LoadSettings();
			void				_SaveSettings();
			void				_ApplySettings(const accessibility_settings& settings);
			void				_UpdateControls(const accessibility_settings& settings);
			BMenuItem*			_MarkTime(BMenuField* field, int32 startMinutes);
			void				_ApplyHighContrast(bool enabled);
			void				_ApplyCursorScale(const accessibility_settings& settings);
			void				_ApplyMonoAudio(bool enabled);

			accessibility_settings	fSettings;
			accessibility_settings	fSavedSettings;

			// User colours captured before high contrast is applied.
			BMessage			fSavedUserColors;

			BCheckBox*			fHighContrast;
			BCheckBox*			fCursorOverride;
			BSlider*			fCursorScale;
			BCheckBox*			fReduceMotion;

			BCheckBox*			fStickyKeys;
			BCheckBox*			fSlowKeys;
			BSlider*			fSlowKeysDelay;
			BCheckBox*			fMouseKeys;
			BMenuField*			fMouseKeysSpeed;

			BCheckBox*			fVisualAlerts;
			BCheckBox*			fMonoAudio;

			BCheckBox*			fTempSchedule;
			BMenuField*			fTempDayMenu;
			BMenuField*			fTempNightMenu;
			BTextControl*		fTempDayKelvin;
			BTextControl*		fTempNightKelvin;

			BButton*			fOpenAppearance;
};


#endif	// ACCESSIBILITY_VIEW_H
