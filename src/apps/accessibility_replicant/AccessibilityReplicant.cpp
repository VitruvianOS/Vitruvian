/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Deskbar quick-toggle replicant for V\OS accessibility settings.
 */


#include "AccessibilityReplicant.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <Entry.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include "AccessibilitySettings.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AccessibilityReplicant"


static const char* kSignature = "application/x-vnd.vos-AccessibilityReplicant";
static const char* kAccessibilitySignature = "application/x-vnd.vos-Accessibility";
static const char* kReplicantName = "AccessibilityReplicant";

static const uint32 kMsgToggle = 'atgl';
static const uint32 kMsgOpenSettings = 'aopn';


status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char*)our_image >= (char*)image.text
			&& (char*)our_image <= (char*)image.text + image.text_size)
			return B_OK;
	}
	return B_ERROR;
}


static BBitmap*
_CreateIconBitmap()
{
	const int32 kSize = 16;
	BBitmap* bitmap = new BBitmap(BRect(0, 0, kSize - 1, kSize - 1),
		B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}

	rgb_color transparent = make_color(0, 0, 0, 0);
	rgb_color white = make_color(255, 255, 255, 255);
	rgb_color blue = make_color(0, 120, 255, 255);
	uint8* bits = (uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	for (int32 y = 0; y < kSize; y++) {
		for (int32 x = 0; x < kSize; x++) {
			rgb_color c = transparent;
			if (y >= 3 && y <= 12 && (x == 3 || x == 12))
				c = white;
			if (y >= 3 && y <= 4 && x >= 3 && x <= 12)
				c = white;
			if (y >= 8 && y <= 9 && x >= 5 && x <= 10)
				c = white;
			if (y == 13 && x >= 4 && x <= 11)
				c = blue;
			uint8* p = bits + y * bpr + x * 4;
			p[0] = c.blue;
			p[1] = c.green;
			p[2] = c.red;
			p[3] = c.alpha;
		}
	}
	return bitmap;
}


AccessibilityReplicant::AccessibilityReplicant(BRect frame, int32 resizingMode,
	bool inDeskbar)
	:
	BView(frame, kReplicantName, resizingMode, B_WILL_DRAW
		| B_FRAME_EVENTS | B_PULSE_NEEDED),
	fIcon(NULL),
	fInDeskbar(inDeskbar)
{
	fIcon = _CreateIconBitmap();
	_SetLabel(B_TRANSLATE("Accessibility"));
}


AccessibilityReplicant::AccessibilityReplicant(BMessage* archive)
	:
	BView(archive),
	fIcon(NULL),
	fInDeskbar(true)
{
	_SetLabel(B_TRANSLATE("Accessibility"));
	fIcon = _CreateIconBitmap();
}


AccessibilityReplicant::~AccessibilityReplicant()
{
	delete fIcon;
}


BView*
AccessibilityReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "AccessibilityReplicant"))
		return NULL;
	return new(std::nothrow) AccessibilityReplicant(archive);
}


status_t
AccessibilityReplicant::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "AccessibilityReplicant");
	return status;
}


void
AccessibilityReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Window() != NULL)
		Window()->SetPulseRate(1000000);
}


void
AccessibilityReplicant::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
AccessibilityReplicant::Pulse()
{
	Invalidate();
}


void
AccessibilityReplicant::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(bounds);

	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fIcon, bounds.LeftTop());
		SetDrawingMode(B_OP_COPY);
	} else {
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString("A", bounds.LeftTop() + BPoint(4, 12));
	}
}


void
AccessibilityReplicant::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggle:
		{
			const char* key = NULL;
			if (message->FindString("key", &key) != B_OK)
				break;
			accessibility_settings settings;
			accessibility_load_settings(settings);
			if (strcmp(key, "sticky") == 0)
				settings.sticky_keys = !settings.sticky_keys;
			else if (strcmp(key, "slow") == 0)
				settings.slow_keys = !settings.slow_keys;
			else if (strcmp(key, "mouse") == 0)
				settings.mouse_keys = !settings.mouse_keys;
			else if (strcmp(key, "contrast") == 0)
				settings.high_contrast = !settings.high_contrast;
			else if (strcmp(key, "visual") == 0)
				settings.visual_alerts = !settings.visual_alerts;
			else if (strcmp(key, "mono") == 0) {
				settings.mono_audio = !settings.mono_audio;
				// Same apply path as the preflet; saving alone does
				// not change what the audio graph does.
				accessibility_apply_mono_audio(settings.mono_audio);
			} else if (strcmp(key, "motion") == 0)
				settings.reduce_motion = !settings.reduce_motion;
			accessibility_save_settings(settings);
			Invalidate();
			break;
		}

		case kMsgOpenSettings:
		{
			entry_ref ref;
			if (be_roster->FindApp(kAccessibilitySignature, &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
AccessibilityReplicant::MouseDown(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("a11y_toggles", true, false);
	menu->SetFont(be_plain_font);

	accessibility_settings settings;
	accessibility_load_settings(settings);

	struct ToggleSpec {
		const char*	key;
		const char*	label;
		bool		value;
	};
	ToggleSpec specs[] = {
		{ "sticky", B_TRANSLATE("Sticky keys"), settings.sticky_keys },
		{ "slow", B_TRANSLATE("Slow keys"), settings.slow_keys },
		{ "mouse", B_TRANSLATE("Mouse keys"), settings.mouse_keys },
		{ "contrast", B_TRANSLATE("High contrast"), settings.high_contrast },
		{ "motion", B_TRANSLATE("Reduce motion"), settings.reduce_motion },
		{ "visual", B_TRANSLATE("Visual alerts"), settings.visual_alerts },
		{ "mono", B_TRANSLATE("Mono audio"), settings.mono_audio },
	};

	for (uint32 i = 0; i < B_COUNT_OF(specs); i++) {
		BMessage* message = new BMessage(kMsgToggle);
		message->AddString("key", specs[i].key);
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE_NOCOLLECT(specs[i].label), message);
		if (specs[i].value)
			item->SetMarked(true);
		menu->AddItem(item);
	}

	menu->AddSeparatorItem();
	BMenuItem* settingsItem = new BMenuItem(
		B_TRANSLATE("Accessibility Settings..."),
		new BMessage(kMsgOpenSettings));
	menu->AddItem(settingsItem);

	menu->SetTargetForItems(this);
	menu->Go(ConvertToScreen(where), true, true, false);
	delete menu;
}


void
AccessibilityReplicant::_SetLabel(const char* label)
{
	fLabel = label;
}
