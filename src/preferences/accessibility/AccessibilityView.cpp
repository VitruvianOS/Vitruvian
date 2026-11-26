/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "AccessibilityView.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <DefaultColors.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaGraph.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <ServerReadOnlyMemory.h>
#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>

#include "AccessibilityWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Accessibility"


static const uint32 kMsgHighContrast = 'hghc';
static const uint32 kMsgCursorOverride = 'cuov';
static const uint32 kMsgCursorScale = 'cusl';
static const uint32 kMsgReduceMotion = 'rmot';
static const uint32 kMsgStickyKeys = 'stky';
static const uint32 kMsgSlowKeys = 'slky';
static const uint32 kMsgSlowKeysDelay = 'slkd';
static const uint32 kMsgMouseKeys = 'msky';
static const uint32 kMsgMouseKeysSpeed = 'msks';
static const uint32 kMsgVisualAlerts = 'visl';
static const uint32 kMsgMonoAudio = 'moau';
static const uint32 kMsgTempSchedule = 'tmsc';
static const uint32 kMsgTempDay = 'tmdy';
static const uint32 kMsgTempNight = 'tmng';
static const uint32 kMsgTempDayKelvin = 'tmkD';
static const uint32 kMsgTempNightKelvin = 'tmkN';
static const uint32 kMsgOpenAppearance = 'opap';

static const char* kAppearanceSignature = "application/x-vnd.vos-Appearance";


struct TimeChoice {
	const char*	label;
	int32		minutes;
};


static const TimeChoice kTimeChoices[] = {
	{ B_TRANSLATE_MARK("00:00"), 0 },
	{ B_TRANSLATE_MARK("06:00"), 6 * 60 },
	{ B_TRANSLATE_MARK("08:00"), 8 * 60 },
	{ B_TRANSLATE_MARK("12:00"), 12 * 60 },
	{ B_TRANSLATE_MARK("17:00"), 17 * 60 },
	{ B_TRANSLATE_MARK("19:00"), 19 * 60 },
	{ B_TRANSLATE_MARK("20:00"), 20 * 60 },
	{ B_TRANSLATE_MARK("21:00"), 21 * 60 },
	{ B_TRANSLATE_MARK("22:00"), 22 * 60 },
};


static void
fill_time_menu(BPopUpMenu* menu, BMessage* message, int32 selected)
{
	for (uint32 i = 0; i < B_COUNT_OF(kTimeChoices); i++) {
		BMessage* choiceMessage = new BMessage(*message);
		choiceMessage->AddInt32("minutes", kTimeChoices[i].minutes);
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE_NOCOLLECT(kTimeChoices[i].label), choiceMessage);
		menu->AddItem(item);
		if (kTimeChoices[i].minutes == selected)
			item->SetMarked(true);
	}
}


static void
fill_mouse_speed_menu(BPopUpMenu* menu, BMessage* message, int32 selected)
{
	static const char* kLabels[] = {
		B_TRANSLATE_MARK("Slow"),
		B_TRANSLATE_MARK("Medium slow"),
		B_TRANSLATE_MARK("Medium"),
		B_TRANSLATE_MARK("Medium fast"),
		B_TRANSLATE_MARK("Fast"),
	};
	for (int32 i = 0; i < 5; i++) {
		BMessage* choiceMessage = new BMessage(*message);
		choiceMessage->AddInt32("speed", i);
		BMenuItem* item = new BMenuItem(B_TRANSLATE_NOCOLLECT(kLabels[i]),
			choiceMessage);
		menu->AddItem(item);
		if (i == selected)
			item->SetMarked(true);
	}
}


static void
add_color(BMessage& message, color_which which, uint8 r, uint8 g, uint8 b)
{
	rgb_color color = make_color(r, g, b, 255);
	message.SetColor(ui_color_name(which), color);
}


static void
build_high_contrast_colors(BMessage& message)
{
	// One-click scheme that writes the existing system colour slots.
	add_color(message, B_PANEL_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_PANEL_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_DOCUMENT_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_DOCUMENT_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_CONTROL_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_CONTROL_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_CONTROL_BORDER_COLOR, 255, 255, 255);
	add_color(message, B_CONTROL_HIGHLIGHT_COLOR, 0, 255, 255);
	add_color(message, B_CONTROL_MARK_COLOR, 255, 255, 0);
	add_color(message, B_NAVIGATION_BASE_COLOR, 255, 255, 0);
	add_color(message, B_NAVIGATION_PULSE_COLOR, 255, 0, 0);
	add_color(message, B_SHINE_COLOR, 255, 255, 255);
	add_color(message, B_SHADOW_COLOR, 0, 0, 0);
	add_color(message, B_LINK_TEXT_COLOR, 0, 255, 255);
	add_color(message, B_LINK_HOVER_COLOR, 255, 255, 0);
	add_color(message, B_LINK_VISITED_COLOR, 255, 0, 255);
	add_color(message, B_LINK_ACTIVE_COLOR, 255, 128, 0);
	add_color(message, B_MENU_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_MENU_SELECTED_BACKGROUND_COLOR, 255, 255, 255);
	add_color(message, B_MENU_ITEM_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_MENU_SELECTED_ITEM_TEXT_COLOR, 0, 0, 0);
	add_color(message, B_MENU_SELECTED_BORDER_COLOR, 255, 255, 0);
	add_color(message, B_LIST_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_LIST_SELECTED_BACKGROUND_COLOR, 255, 255, 255);
	add_color(message, B_LIST_ITEM_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_LIST_SELECTED_ITEM_TEXT_COLOR, 0, 0, 0);
	add_color(message, B_SCROLL_BAR_THUMB_COLOR, 255, 255, 255);
	add_color(message, B_TOOL_TIP_BACKGROUND_COLOR, 0, 0, 0);
	add_color(message, B_TOOL_TIP_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_STATUS_BAR_COLOR, 0, 128, 255);
	add_color(message, B_SUCCESS_COLOR, 0, 255, 0);
	add_color(message, B_FAILURE_COLOR, 255, 0, 0);
	add_color(message, B_WINDOW_TAB_COLOR, 0, 0, 255);
	add_color(message, B_WINDOW_TEXT_COLOR, 255, 255, 255);
	add_color(message, B_WINDOW_INACTIVE_TAB_COLOR, 64, 64, 64);
	add_color(message, B_WINDOW_INACTIVE_TEXT_COLOR, 192, 192, 192);
	add_color(message, B_WINDOW_BORDER_COLOR, 255, 255, 255);
	add_color(message, B_WINDOW_INACTIVE_BORDER_COLOR, 128, 128, 128);
}


static void
build_default_colors(BMessage& message)
{
	for (int32 i = 0; i < kColorWhichCount; i++) {
		color_which which = index_to_color_which(i);
		if (which == B_NO_COLOR)
			continue;
		message.SetColor(ui_color_name(which), BPrivate::kDefaultColors[i]);
	}
}


static void
send_cursor_scale(float scale, bool override)
{
	// scale <= 0 clears the override and returns to the font-derived default.
	set_cursor_scale(override ? scale : 0.0f);
}


AccessibilityView::AccessibilityView()
	:
	BGroupView(B_TRANSLATE("Accessibility")),
	fSettings(kDefaultAccessibilitySettings),
	fSavedSettings(kDefaultAccessibilitySettings)
{
	fHighContrast = new BCheckBox("high_contrast",
		B_TRANSLATE("High-contrast colour scheme"),
		new BMessage(kMsgHighContrast));

	fCursorOverride = new BCheckBox("cursor_override",
		B_TRANSLATE("Larger cursor size"), new BMessage(kMsgCursorOverride));
	fCursorScale = new BSlider("cursor_scale", B_TRANSLATE("Cursor scale:"),
		new BMessage(kMsgCursorScale), 100, 400, B_HORIZONTAL);
	fCursorScale->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fCursorScale->SetHashMarkCount(4);
	fCursorScale->SetLimitLabels(B_TRANSLATE("1x"), B_TRANSLATE("4x"));
	fCursorScale->SetToolTip(B_TRANSLATE(
		"Leave unchecked to keep the cursor size that follows the font."));

	fReduceMotion = new BCheckBox("reduce_motion",
		B_TRANSLATE("Reduce motion (Deskbar and switcher)"),
		new BMessage(kMsgReduceMotion));

	fStickyKeys = new BCheckBox("sticky_keys", B_TRANSLATE("Sticky keys"),
		new BMessage(kMsgStickyKeys));
	fStickyKeys->SetToolTip(B_TRANSLATE(
		"Press Shift three times to toggle."));

	fSlowKeys = new BCheckBox("slow_keys", B_TRANSLATE("Slow keys"),
		new BMessage(kMsgSlowKeys));
	fSlowKeysDelay = new BSlider("slow_keys_delay",
		B_TRANSLATE("Slow keys delay:"), new BMessage(kMsgSlowKeysDelay),
		10, 200, B_HORIZONTAL);
	fSlowKeysDelay->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fSlowKeysDelay->SetHashMarkCount(4);
	fSlowKeysDelay->SetLimitLabels(B_TRANSLATE("0.1s"), B_TRANSLATE("2.0s"));

	fMouseKeys = new BCheckBox("mouse_keys",
		B_TRANSLATE("Mouse keys (numeric keypad)"),
		new BMessage(kMsgMouseKeys));
	BPopUpMenu* speedMenu = new BPopUpMenu("mouse_keys_speed");
	fill_mouse_speed_menu(speedMenu, new BMessage(kMsgMouseKeysSpeed), 2);
	fMouseKeysSpeed = new BMenuField("mouse_keys_speed_field",
		B_TRANSLATE("Mouse keys speed:"), speedMenu);
	fMouseKeysSpeed->SetAlignment(B_ALIGN_RIGHT);
	fMouseKeysSpeed->SetToolTip(B_TRANSLATE(
		"Use the numeric keypad to move the pointer and click."));

	fVisualAlerts = new BCheckBox("visual_alerts",
		B_TRANSLATE("Visual alerts instead of beeps"),
		new BMessage(kMsgVisualAlerts));

	fMonoAudio = new BCheckBox("mono_audio",
		B_TRANSLATE("Mono audio (equalize output channels)"),
		new BMessage(kMsgMonoAudio));

	fTempSchedule = new BCheckBox("temp_schedule",
		B_TRANSLATE("Colour-temperature schedule"),
		new BMessage(kMsgTempSchedule));

	BPopUpMenu* dayMenu = new BPopUpMenu("temp_day");
	fill_time_menu(dayMenu, new BMessage(kMsgTempDay), 8 * 60);
	fTempDayMenu = new BMenuField("temp_day_field",
		B_TRANSLATE("Day starts:"), dayMenu);
	fTempDayMenu->SetAlignment(B_ALIGN_RIGHT);

	BPopUpMenu* nightMenu = new BPopUpMenu("temp_night");
	fill_time_menu(nightMenu, new BMessage(kMsgTempNight), 20 * 60);
	fTempNightMenu = new BMenuField("temp_night_field",
		B_TRANSLATE("Night starts:"), nightMenu);
	fTempNightMenu->SetAlignment(B_ALIGN_RIGHT);

	fTempDayKelvin = new BTextControl("temp_day_kelvin",
		B_TRANSLATE("Day temperature (K):"), "6500",
		new BMessage(kMsgTempDayKelvin));
	fTempNightKelvin = new BTextControl("temp_night_kelvin",
		B_TRANSLATE("Night temperature (K):"), "4500",
		new BMessage(kMsgTempNightKelvin));

	fOpenAppearance = new BButton("open_appearance",
		B_TRANSLATE("Open Appearance..."), new BMessage(kMsgOpenAppearance));

	BStringView* fontsHint = new BStringView("fonts_hint",
		B_TRANSLATE("Font size stays in Appearance, Fonts."));
	BStringView* soundsHint = new BStringView("sounds_hint",
		B_TRANSLATE("Sound file mapping is under Media, Sounds."));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fHighContrast)
			.Add(fCursorOverride)
			.Add(fCursorScale)
			.Add(fReduceMotion)
			.Add(fontsHint)
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fStickyKeys)
			.Add(fSlowKeys)
			.Add(fSlowKeysDelay)
			.Add(fMouseKeys)
			.Add(fMouseKeysSpeed)
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fVisualAlerts)
			.Add(fMonoAudio)
			.Add(soundsHint)
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fTempSchedule)
			.Add(fTempDayMenu)
			.Add(fTempNightMenu)
			.Add(fTempDayKelvin)
			.Add(fTempNightKelvin)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fOpenAppearance)
			.AddGlue()
		.End();
}


AccessibilityView::~AccessibilityView()
{
}


void
AccessibilityView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fHighContrast->SetTarget(this);
	fCursorOverride->SetTarget(this);
	fCursorScale->SetTarget(this);
	fReduceMotion->SetTarget(this);
	fStickyKeys->SetTarget(this);
	fSlowKeys->SetTarget(this);
	fSlowKeysDelay->SetTarget(this);
	fMouseKeys->SetTarget(this);
	fVisualAlerts->SetTarget(this);
	fMonoAudio->SetTarget(this);
	fTempSchedule->SetTarget(this);
	fTempDayKelvin->SetTarget(this);
	fTempNightKelvin->SetTarget(this);
	fOpenAppearance->SetTarget(this);

	_LoadSettings();
	_UpdateControls(fSettings);
	fSavedSettings = fSettings;
}


void
AccessibilityView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgHighContrast:
			fSettings.high_contrast
				= fHighContrast->Value() == B_CONTROL_ON;
			_ApplyHighContrast(fSettings.high_contrast);
			_SaveSettings();
			break;

		case kMsgCursorOverride:
			fSettings.cursor_override
				= fCursorOverride->Value() == B_CONTROL_ON;
			_ApplyCursorScale(fSettings);
			_SaveSettings();
			break;

		case kMsgCursorScale:
			fSettings.cursor_scale = fCursorScale->Value() / 100.0f;
			_ApplyCursorScale(fSettings);
			_SaveSettings();
			break;

		case kMsgReduceMotion:
			fSettings.reduce_motion
				= fReduceMotion->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgStickyKeys:
			fSettings.sticky_keys = fStickyKeys->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgSlowKeys:
			fSettings.slow_keys = fSlowKeys->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgSlowKeysDelay:
			fSettings.slow_keys_delay = fSlowKeysDelay->Value() / 100.0f;
			_SaveSettings();
			break;

		case kMsgMouseKeys:
			fSettings.mouse_keys = fMouseKeys->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgMouseKeysSpeed:
		{
			BMenuItem* item = fMouseKeysSpeed->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL)
				fSettings.mouse_keys_speed
					= item->Message()->GetInt32("speed", 2);
			_SaveSettings();
			break;
		}

		case kMsgVisualAlerts:
			fSettings.visual_alerts
				= fVisualAlerts->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgMonoAudio:
			fSettings.mono_audio = fMonoAudio->Value() == B_CONTROL_ON;
			_ApplyMonoAudio(fSettings.mono_audio);
			_SaveSettings();
			break;

		case kMsgTempSchedule:
			fSettings.temp_schedule_enabled
				= fTempSchedule->Value() == B_CONTROL_ON;
			_SaveSettings();
			break;

		case kMsgTempDay:
		{
			BMenuItem* item = fTempDayMenu->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL)
				fSettings.temp_day_start
					= item->Message()->GetInt32("minutes", 8 * 60);
			_SaveSettings();
			break;
		}

		case kMsgTempNight:
		{
			BMenuItem* item = fTempNightMenu->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL)
				fSettings.temp_night_start
					= item->Message()->GetInt32("minutes", 20 * 60);
			_SaveSettings();
			break;
		}

		case kMsgTempDayKelvin:
			fSettings.temp_day_kelvin
				= atof(fTempDayKelvin->Text());
			_SaveSettings();
			break;

		case kMsgTempNightKelvin:
			fSettings.temp_night_kelvin
				= atof(fTempNightKelvin->Text());
			_SaveSettings();
			break;

		case kMsgOpenAppearance:
		{
			entry_ref ref;
			if (be_roster->FindApp(kAppearanceSignature, &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}

	Window()->PostMessage(kAccessibilitySettingsChanged);
}


void
AccessibilityView::SetDefaults()
{
	fSettings = kDefaultAccessibilitySettings;
	_ApplySettings(fSettings);
	_SaveSettings();
	_UpdateControls(fSettings);
}


void
AccessibilityView::Revert()
{
	fSettings = fSavedSettings;
	_ApplySettings(fSettings);
	_SaveSettings();
	_UpdateControls(fSettings);
}


bool
AccessibilityView::IsDefaultable() const
{
	return memcmp(&fSettings, &kDefaultAccessibilitySettings,
		sizeof(fSettings)) != 0;
}


bool
AccessibilityView::IsRevertable() const
{
	return memcmp(&fSettings, &fSavedSettings, sizeof(fSettings)) != 0;
}


void
AccessibilityView::_LoadSettings()
{
	accessibility_load_settings(fSettings);
}


void
AccessibilityView::_SaveSettings()
{
	accessibility_save_settings(fSettings);
	Window()->PostMessage('kUpd');
}


void
AccessibilityView::_ApplySettings(const accessibility_settings& settings)
{
	_ApplyHighContrast(settings.high_contrast);
	_ApplyCursorScale(settings);
	_ApplyMonoAudio(settings.mono_audio);
}


void
AccessibilityView::_UpdateControls(const accessibility_settings& settings)
{
	fHighContrast->SetValue(settings.high_contrast
		? B_CONTROL_ON : B_CONTROL_OFF);
	fCursorOverride->SetValue(settings.cursor_override
		? B_CONTROL_ON : B_CONTROL_OFF);
	fCursorScale->SetValue((int32)(settings.cursor_scale * 100.0f + 0.5f));
	fCursorScale->SetEnabled(settings.cursor_override);
	fReduceMotion->SetValue(settings.reduce_motion
		? B_CONTROL_ON : B_CONTROL_OFF);

	fStickyKeys->SetValue(settings.sticky_keys
		? B_CONTROL_ON : B_CONTROL_OFF);
	fSlowKeys->SetValue(settings.slow_keys
		? B_CONTROL_ON : B_CONTROL_OFF);
	fSlowKeysDelay->SetValue((int32)(settings.slow_keys_delay * 100.0f
		+ 0.5f));
	fSlowKeysDelay->SetEnabled(settings.slow_keys);
	fMouseKeys->SetValue(settings.mouse_keys
		? B_CONTROL_ON : B_CONTROL_OFF);
	fMouseKeysSpeed->SetEnabled(settings.mouse_keys);
	BMenuItem* speedItem = fMouseKeysSpeed->Menu()->ItemAt(
		settings.mouse_keys_speed);
	if (speedItem != NULL)
		speedItem->SetMarked(true);

	fVisualAlerts->SetValue(settings.visual_alerts
		? B_CONTROL_ON : B_CONTROL_OFF);
	fMonoAudio->SetValue(settings.mono_audio
		? B_CONTROL_ON : B_CONTROL_OFF);

	fTempSchedule->SetValue(settings.temp_schedule_enabled
		? B_CONTROL_ON : B_CONTROL_OFF);
	fTempDayMenu->SetEnabled(settings.temp_schedule_enabled);
	fTempNightMenu->SetEnabled(settings.temp_schedule_enabled);

	BMenuItem* dayItem = _MarkTime(fTempDayMenu, settings.temp_day_start);
	if (dayItem != NULL)
		dayItem->SetMarked(true);
	BMenuItem* nightItem = _MarkTime(fTempNightMenu,
		settings.temp_night_start);
	if (nightItem != NULL)
		nightItem->SetMarked(true);

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%d", (int)settings.temp_day_kelvin);
	fTempDayKelvin->SetText(buffer);
	fTempDayKelvin->SetEnabled(settings.temp_schedule_enabled);
	snprintf(buffer, sizeof(buffer), "%d", (int)settings.temp_night_kelvin);
	fTempNightKelvin->SetText(buffer);
	fTempNightKelvin->SetEnabled(settings.temp_schedule_enabled);
}


BMenuItem*
AccessibilityView::_MarkTime(BMenuField* field, int32 startMinutes)
{
	BMenu* menu = field->Menu();
	if (menu == NULL)
		return NULL;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() != NULL
			&& item->Message()->GetInt32("minutes", -1) == startMinutes)
			return item;
	}
	return NULL;
}


void
AccessibilityView::_ApplyHighContrast(bool enabled)
{
	if (enabled) {
		// Capture the user's current colours once so turning the
		// scheme off restores them instead of the tree defaults.
		if (fSavedUserColors.CountNames(B_RGB_32_BIT_TYPE) == 0) {
			BMessage stored;
			if (accessibility_load_high_contrast_colors(stored) == B_OK) {
				fSavedUserColors = stored;
			} else {
				for (int32 i = 0; i < kColorWhichCount; i++) {
					color_which which = index_to_color_which(i);
					if (which == B_NO_COLOR)
						continue;
					const char* name = ui_color_name(which);
					if (name != NULL)
						fSavedUserColors.SetColor(name, ui_color(which));
				}
				accessibility_save_settings(fSettings, &fSavedUserColors);
			}
		}
		BMessage colors;
		build_high_contrast_colors(colors);
		set_ui_colors(&colors);
		return;
	}

	BMessage restore;
	if (fSavedUserColors.CountNames(B_RGB_32_BIT_TYPE) > 0)
		restore = fSavedUserColors;
	else if (accessibility_load_high_contrast_colors(restore) != B_OK)
		restore.MakeEmpty();

	if (restore.CountNames(B_RGB_32_BIT_TYPE) > 0) {
		set_ui_colors(&restore);
		fSavedUserColors.MakeEmpty();
		// Drop the snapshot so the next enable captures afresh.
		BMessage empty;
		accessibility_save_settings(fSettings, &empty);
		return;
	}

	BMessage colors;
	build_default_colors(colors);
	set_ui_colors(&colors);
}


void
AccessibilityView::_ApplyCursorScale(const accessibility_settings& settings)
{
	send_cursor_scale(settings.cursor_scale, settings.cursor_override);
}


void
AccessibilityView::_ApplyMonoAudio(bool enabled)
{
	accessibility_apply_mono_audio(enabled);
}
