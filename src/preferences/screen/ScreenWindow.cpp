/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Andrew Bachmann
 *		Stefano Ceccherini, burton666@libero.it
 *		Alexandre Deckner, alex@zappotek.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent, rene@gollent.com
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */


#include "ScreenWindow.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <InterfacePrivate.h>
#include <PanelOrientationTransform.h>
#include <PrivateScreen.h>

#include "AlertWindow.h"
#include "Constants.h"
#include "RefreshWindow.h"
#include "MonitorView.h"
#include "ScreenSettings.h"
#include "Utility.h"

/* Note, this headers defines a *private* interface to the Radeon accelerant.
 * It's a solution that works with the current BeOS interface that Haiku
 * adopted.
 * However, it's not a nice and clean solution. Don't use this header in any
 * application if you can avoid it. No other driver is using this, or should
 * be using this.
 * It will be replaced as soon as we introduce an updated accelerant interface
 * which may even happen before R1 hits the streets.
 */
// Radeon multi-monitor tunnel not available; all multimon UI is hidden at runtime.
#include "multimon.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


const char* kBackgroundsSignature = "application/x-vnd.Haiku-Backgrounds";

// list of officially supported colour spaces
static const struct {
	color_space	space;
	int32		bits_per_pixel;
	const char*	label;
} kColorSpaces[] = {
	{ B_CMAP8, 8, B_TRANSLATE("8 bits/pixel, 256 colors") },
	{ B_RGB15, 15, B_TRANSLATE("15 bits/pixel, 32768 colors") },
	{ B_RGB16, 16, B_TRANSLATE("16 bits/pixel, 65536 colors") },
	{ B_RGB24, 24, B_TRANSLATE("24 bits/pixel, 16 Million colors") },
	{ B_RGB32, 32, B_TRANSLATE("32 bits/pixel, 16 Million colors") }
};
static const int32 kColorSpaceCount = B_COUNT_OF(kColorSpaces);

// list of standard refresh rates
static const int32 kRefreshRates[] = { 60, 70, 72, 75, 80, 85, 95, 100 };
static const int32 kRefreshRateCount = B_COUNT_OF(kRefreshRates);

// list of combine modes
static const struct {
	combine_mode	mode;
	const char		*name;
} kCombineModes[] = {
	{ kCombineDisable, B_TRANSLATE("disable") },
	{ kCombineHorizontally, B_TRANSLATE("horizontally") },
	{ kCombineVertically, B_TRANSLATE("vertically") }
};
static const int32 kCombineModeCount = B_COUNT_OF(kCombineModes);


static BString
tv_standard_to_string(uint32 mode)
{
	switch (mode) {
		case 0:		return "disabled";
		case 1:		return "NTSC";
		case 2:		return "NTSC Japan";
		case 3:		return "PAL BDGHI";
		case 4:		return "PAL M";
		case 5:		return "PAL N";
		case 6:		return "SECAM";
		case 101:	return "NTSC 443";
		case 102:	return "PAL 60";
		case 103:	return "PAL NC";
		default:
		{
			BString name;
			name << "??? (" << mode << ")";

			return name;
		}
	}
}


static void
resolution_to_string(screen_mode& mode, BString &string)
{
	string.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);
}


static void
refresh_rate_to_string(float refresh, BString &string,
	bool appendUnit = true, bool alwaysWithFraction = false)
{
	snprintf(string.LockBuffer(32), 32, "%.*g", refresh >= 100.0 ? 4 : 3,
		refresh);
	string.UnlockBuffer();

	if (appendUnit)
		string << " " << B_TRANSLATE("Hz");
}


static const char*
screen_errors(status_t status)
{
	switch (status) {
		case B_ENTRY_NOT_FOUND:
			return B_TRANSLATE("Unknown mode");
		// TODO: add more?

		default:
			return strerror(status);
	}
}


//	#pragma mark - ScreenWindow


ScreenWindow::ScreenWindow(ScreenSettings* settings)
	:
	BWindow(settings->WindowFrame(), B_TRANSLATE_SYSTEM_NAME("Screen"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS, B_ALL_WORKSPACES),
	fIsVesa(false),
	fBootWorkspaceApplied(false),
	fUserSelectedColorSpace(NULL),
	fOtherRefresh(NULL),
	fScreenMode(this),
	fUndoScreenMode(this),
	fModified(false)
{
	BScreen screen(this);

	accelerant_device_info info;
	if (screen.GetDeviceInfo(&info) == B_OK
		&& !strcasecmp(info.chipset, "VESA"))
		fIsVesa = true;

	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;

	fSettings = settings;

	// we need the "Current Workspace" first to get its height

	BPopUpMenu* popUpMenu = new BPopUpMenu(B_TRANSLATE("Current workspace"),
		true, true);
	fAllWorkspacesItem = new BMenuItem(B_TRANSLATE("All workspaces"),
		new BMessage(WORKSPACE_CHECK_MSG));
	popUpMenu->AddItem(fAllWorkspacesItem);
	BMenuItem *item = new BMenuItem(B_TRANSLATE("Current workspace"),
		new BMessage(WORKSPACE_CHECK_MSG));

	popUpMenu->AddItem(item);
	fAllWorkspacesItem->SetMarked(true);

	BMenuField* workspaceMenuField = new BMenuField("WorkspaceMenu", NULL,
		popUpMenu);
	workspaceMenuField->ResizeToPreferred();

	// box on the left with workspace count and monitor view

	fScreenBox = new BBox("screen box");
	BGroupView* groupView = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fScreenBox->AddChild(groupView);
	fScreenBox->SetLabel("placeholder");
		// Needed for layouting, will be replaced with screen name/size
	groupView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	fDeviceInfo = new BStringView("device info", "");
	fDeviceInfo->SetAlignment(B_ALIGN_CENTER);
	groupView->AddChild(fDeviceInfo);

	float scaling = std::max(1.0f, be_plain_font->Size() / 12.0f);
	fMonitorView = new MonitorView(BRect(0.0, 0.0, 80.0 * scaling,
			80.0 * scaling), "monitor", screen.Frame().IntegerWidth() + 1,
		screen.Frame().IntegerHeight() + 1);
	fMonitorView->SetToolTip(B_TRANSLATE("Set background" B_UTF8_ELLIPSIS));
	groupView->AddChild(fMonitorView);

	// brightness slider
	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness:"),
		NULL, 0, 255, B_HORIZONTAL);
	groupView->AddChild(fBrightnessSlider);

	if (screen.GetBrightness(&fOriginalBrightness) == B_OK) {
		fBrightnessSlider->SetModificationMessage(
			new BMessage(SLIDER_BRIGHTNESS_MSG));
		fBrightnessSlider->SetValue(fOriginalBrightness * 255);
	} else {
		// The driver does not support changing the brightness,
		// so hide the slider
		fBrightnessSlider->Hide();
		fOriginalBrightness = -1;
	}

	// colour temperature presets; hidden when the output has no gamma LUT

	fTemperature = 6500.0f;
	fOriginalTemperature = 6500.0f;
	fTemperatureSupported = false;

	float currentTemp = 6500.0f;
	BScreen tempScreen(this);
	fTemperatureSupported
		= tempScreen.GetTemperature(&currentTemp) == B_OK;
	if (fTemperatureSupported) {
		fTemperature = currentTemp;
		fOriginalTemperature = currentTemp;
	}

	struct {
		const char*	label;
		float		kelvin;
	} kTemperaturePresets[] = {
		{ B_TRANSLATE_MARK("Very cold (9000 K)"), 9000.0f },
		{ B_TRANSLATE_MARK("Cold (7500 K)"), 7500.0f },
		{ B_TRANSLATE_MARK("Mild (6500 K)"), 6500.0f },
		{ B_TRANSLATE_MARK("Warm (4500 K)"), 4500.0f },
		{ B_TRANSLATE_MARK("Very warm (3400 K)"), 3400.0f },
	};

	fTemperatureMenu = new BPopUpMenu("temperature", true, true);
	for (uint32 i = 0; i < B_COUNT_OF(kTemperaturePresets); i++) {
		BMessage* message = new BMessage(POP_TEMPERATURE_MSG);
		message->AddFloat("kelvin", kTemperaturePresets[i].kelvin);
		fTemperatureMenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(kTemperaturePresets[i].label), message));
	}

	fTemperatureField = new BMenuField("TemperatureMenu",
		B_TRANSLATE("Colour temperature:"), fTemperatureMenu);
	fTemperatureField->SetAlignment(B_ALIGN_RIGHT);
	_MarkTemperaturePreset(fTemperature);

	if (!fTemperatureSupported) {
		fTemperatureField->Hide();
		fTemperatureField->SetToolTip(B_TRANSLATE(
			"This display does not support colour temperature."));
	}

	// box on the left below the screen box with workspaces

	BBox* workspacesBox = new BBox("workspaces box");
	workspacesBox->SetLabel(B_TRANSLATE("Workspaces"));

	BGroupLayout* workspacesLayout = new BGroupLayout(B_VERTICAL);
	workspacesLayout->SetInsets(B_USE_DEFAULT_SPACING,
		be_control_look->DefaultItemSpacing() * 2, B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING);
	workspacesBox->SetLayout(workspacesLayout);

	fColumnsControl = new BSpinner("columns", B_TRANSLATE("Columns:"),
		new BMessage(kMsgWorkspaceColumnsChanged));
	fColumnsControl->SetAlignment(B_ALIGN_RIGHT);
	fColumnsControl->SetRange(1, 32);

	fRowsControl = new BSpinner("rows", B_TRANSLATE("Rows:"),
		new BMessage(kMsgWorkspaceRowsChanged));
	fRowsControl->SetAlignment(B_ALIGN_RIGHT);
	fRowsControl->SetRange(1, 32);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);
	fColumnsControl->SetValue(columns);
	fRowsControl->SetValue(rows);

	workspacesBox->AddChild(BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
					// columns
					.Add(fColumnsControl->CreateLabelLayoutItem(), 0, 0)
					.Add(fColumnsControl->CreateTextViewLayoutItem(), 1, 0)
					// rows
					.Add(fRowsControl->CreateLabelLayoutItem(), 0, 1)
					.Add(fRowsControl->CreateTextViewLayoutItem(), 1, 1)
					.End()
				.AddGlue()
				.End()
			.End()
		.View());

	// put workspaces slider in a vertical group with a half space above so
	// if hidden you won't see the extra space.
	BView* workspacesView = BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(workspacesBox)
		.View();

	// box on the right with screen resolution, etc.

	BBox* controlsBox = new BBox("controls box");
	controlsBox->SetLabel(workspaceMenuField);
	BGroupView* outerControlsView = new BGroupView(B_VERTICAL);
	outerControlsView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	controlsBox->AddChild(outerControlsView);

	menu_layout layout = B_ITEMS_IN_COLUMN;

	// There are modes in the list with the same resolution but different bpp or refresh rates.
	// We don't want to take these into account when computing the menu layout, so we need to
	// count how many entries we will really have in the menu.
	int fullModeCount = fScreenMode.CountModes();
	int modeCount = 0;
	int index = 0;
	uint16 maxWidth = 0;
	uint16 maxHeight = 0;
	uint16 previousWidth = 0;
	uint16 previousHeight = 0;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);

		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;
		modeCount++;
		previousWidth = mode.width;
		previousHeight = mode.height;
		if (maxWidth < mode.width)
			maxWidth = mode.width;
		if (maxHeight < mode.height)
			maxHeight = mode.height;
	}

	if (modeCount > 16)
		layout = B_ITEMS_IN_MATRIX;

	fResolutionMenu = new BPopUpMenu("resolution", true, true, layout);

	// Compute the size we should allocate to each item in the menu
	BRect itemRect;
	if (layout == B_ITEMS_IN_MATRIX) {
		BFont menuFont;
		font_height fontHeight;

		fResolutionMenu->GetFont(&menuFont);
		menuFont.GetHeight(&fontHeight);
		itemRect.left = itemRect.top = 0;
		itemRect.bottom = fontHeight.ascent + fontHeight.descent + 4;
		itemRect.right = menuFont.StringWidth("99999x99999") + 16;
		rows = modeCount / 3 + 1;
	}

	index = 0;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);

		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;

		previousWidth = mode.width;
		previousHeight = mode.height;

		BMessage* message = new BMessage(POP_RESOLUTION_MSG);
		message->AddInt32("width", mode.width);
		message->AddInt32("height", mode.height);

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		if (layout == B_ITEMS_IN_COLUMN)
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
		else {
			int y = index % rows;
			int x = index / rows;
			itemRect.OffsetTo(x * itemRect.Width(), y * itemRect.Height());
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message), itemRect);
		}

		index++;
	}

	fMonitorView->SetMaxResolution(maxWidth, maxHeight);

	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	fColorsMenu = new BPopUpMenu("colors", true, false);

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		BMessage* message = new BMessage(POP_COLORS_MSG);
		message->AddInt32("bits_per_pixel", kColorSpaces[i].bits_per_pixel);
		message->AddInt32("space", kColorSpaces[i].space);

		BMenuItem* item = new BMenuItem(kColorSpaces[i].label, message);
		if (kColorSpaces[i].space == screen.ColorSpace())
			fUserSelectedColorSpace = item;

		fColorsMenu->AddItem(item);
	}

	fColorsField = new BMenuField("ColorsMenu", B_TRANSLATE("Colors:"),
		fColorsMenu);
	fColorsField->SetAlignment(B_ALIGN_RIGHT);

	fRefreshMenu = new BPopUpMenu("refresh rate", true, true);

	float min, max;
	if (fScreenMode.GetRefreshLimits(fActive, min, max) != B_OK) {
		// if we couldn't obtain the refresh limits, reset to the default
		// range. Constraints from detected monitors will fine-tune this
		// later.
		min = kRefreshRates[0];
		max = kRefreshRates[kRefreshRateCount - 1];
	}

	if (min == max) {
		// This is a special case for drivers that only support a single
		// frequency, like the VESA driver
		BString name;
		refresh_rate_to_string(min, name);
		BMessage *message = new BMessage(POP_REFRESH_MSG);
		message->AddFloat("refresh", min);
		BMenuItem *item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		item->SetEnabled(false);
	} else {
		monitor_info info;
		if (fScreenMode.GetMonitorInfo(info) == B_OK) {
			min = max_c(info.min_vertical_frequency, min);
			max = min_c(info.max_vertical_frequency, max);
		}

		for (int32 i = 0; i < kRefreshRateCount; ++i) {
			if (kRefreshRates[i] < min || kRefreshRates[i] > max)
				continue;

			BString name;
			name << kRefreshRates[i] << " " << B_TRANSLATE("Hz");

			BMessage *message = new BMessage(POP_REFRESH_MSG);
			message->AddFloat("refresh", kRefreshRates[i]);

			fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fOtherRefresh = new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
			new BMessage(POP_OTHER_REFRESH_MSG));
		fRefreshMenu->AddItem(fOtherRefresh);
	}

	fRefreshField = new BMenuField("RefreshMenu", B_TRANSLATE("Refresh rate:"),
		fRefreshMenu);
	fRefreshField->SetAlignment(B_ALIGN_RIGHT);

	if (_IsVesa())
		fRefreshField->Hide();

	// enlarged area for multi-monitor settings
	{
		bool dummy;
		uint32 dummy32;
		bool multiMonSupport;
		bool useLaptopPanelSupport;
		bool tvStandardSupport;

		multiMonSupport = TestMultiMonSupport(&screen) == B_OK;
		useLaptopPanelSupport = GetUseLaptopPanel(&screen, &dummy) == B_OK;
		tvStandardSupport = GetTVStandard(&screen, &dummy32) == B_OK;

		// even if there is no support, we still create all controls
		// to make sure we don't access NULL pointers later on

		fCombineMenu = new BPopUpMenu("CombineDisplays",
			true, true);

		for (int32 i = 0; i < kCombineModeCount; i++) {
			BMessage *message = new BMessage(POP_COMBINE_DISPLAYS_MSG);
			message->AddInt32("mode", kCombineModes[i].mode);

			fCombineMenu->AddItem(new BMenuItem(kCombineModes[i].name,
				message));
		}

		fCombineField = new BMenuField("CombineMenu",
			B_TRANSLATE("Combine displays:"), fCombineMenu);
		fCombineField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fCombineField->Hide();

		fSwapDisplaysMenu = new BPopUpMenu("SwapDisplays",
			true, true);

		// !order is important - we rely that boolean value == idx
		BMessage *message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", false);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("no"), message));

		message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", true);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("yes"), message));

		fSwapDisplaysField = new BMenuField("SwapMenu",
			B_TRANSLATE("Swap displays:"), fSwapDisplaysMenu);
		fSwapDisplaysField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fSwapDisplaysField->Hide();

		fUseLaptopPanelMenu = new BPopUpMenu("UseLaptopPanel",
			true, true);

		// !order is important - we rely that boolean value == idx
		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", false);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("if needed"),
			message));

		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", true);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("always"),
			message));

		fUseLaptopPanelField = new BMenuField("UseLaptopPanel",
			B_TRANSLATE("Use laptop panel:"), fUseLaptopPanelMenu);
		fUseLaptopPanelField->SetAlignment(B_ALIGN_RIGHT);

		if (!useLaptopPanelSupport)
			fUseLaptopPanelField->Hide();

		fTVStandardMenu = new BPopUpMenu("TVStandard", true, true);

		// arbitrary limit
		uint32 i;
		for (i = 0; i < 100; ++i) {
			uint32 mode;
			if (GetNthSupportedTVStandard(&screen, i, &mode) != B_OK)
				break;

			BString name = tv_standard_to_string(mode);

			message = new BMessage(POP_TV_STANDARD_MSG);
			message->AddInt32("tv_standard", mode);

			fTVStandardMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fTVStandardField = new BMenuField("tv standard",
			B_TRANSLATE("Video format:"), fTVStandardMenu);
		fTVStandardField->SetAlignment(B_ALIGN_RIGHT);

		if (!tvStandardSupport || i == 0)
			fTVStandardField->Hide();
	}

	// The kernel's own names rather than degrees: the direction convention
	// is not verified on hardware yet, so a degree label could lie.
	static const struct {
		const char*	name;
		int32		rotation;
	} kRotations[] = {
		{ B_TRANSLATE_MARK("Auto"), -1 },
		{ B_TRANSLATE_MARK("Normal"), 0 },
		{ B_TRANSLATE_MARK("Upside down"), 1 },
		{ B_TRANSLATE_MARK("Left side up"), 2 },
		{ B_TRANSLATE_MARK("Right side up"), 3 }
	};

	fRotationMenu = new BPopUpMenu("Rotation", true, true);
	for (uint32 i = 0; i < B_COUNT_OF(kRotations); i++) {
		BMessage* message = new BMessage(POP_ROTATION_MSG);
		message->AddInt32("rotation", kRotations[i].rotation);
		fRotationMenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(kRotations[i].name), message));
	}

	fOriginalRotation = fScreenMode.Rotation();
	BMenuItem* rotationItem = fRotationMenu->ItemAt(fOriginalRotation + 1);
	if (rotationItem != NULL)
		rotationItem->SetMarked(true);

	fRotationField = new BMenuField("RotationMenu",
		B_TRANSLATE("Rotation:"), fRotationMenu);
	fRotationField->SetAlignment(B_ALIGN_RIGHT);

	// Unlike kRotations, there is no "Auto" entry: reflection has no
	// hardware-detected state, so the values are the plain menu order.
	static const struct {
		const char*	name;
		int32		reflection;
	} kReflections[] = {
		{ B_TRANSLATE_MARK("None"), B_PANEL_REFLECTION_NONE },
		{ B_TRANSLATE_MARK("Horizontal"), B_PANEL_REFLECTION_X },
		{ B_TRANSLATE_MARK("Vertical"), B_PANEL_REFLECTION_Y },
		{ B_TRANSLATE_MARK("Both"), B_PANEL_REFLECTION_BOTH }
	};

	fReflectionMenu = new BPopUpMenu("Reflection", true, true);
	for (uint32 i = 0; i < B_COUNT_OF(kReflections); i++) {
		BMessage* message = new BMessage(POP_REFLECTION_MSG);
		message->AddInt32("reflection", kReflections[i].reflection);
		fReflectionMenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(kReflections[i].name), message));
	}

	fOriginalReflection = fScreenMode.Reflection();
	_MarkReflectionItem(fOriginalReflection);

	fReflectionField = new BMenuField("ReflectionMenu",
		B_TRANSLATE("Reflection:"), fReflectionMenu);
	fReflectionField->SetAlignment(B_ALIGN_RIGHT);

	BLayoutBuilder::Group<>(outerControlsView)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 1)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 3)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 4)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 5)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 6)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 6)
			.Add(fRotationField->CreateLabelLayoutItem(), 0, 7)
			.Add(fRotationField->CreateMenuBarLayoutItem(), 1, 7)
			.Add(fReflectionField->CreateLabelLayoutItem(), 0, 8)
			.Add(fReflectionField->CreateMenuBarLayoutItem(), 1, 8)
		.End()
		.Add(fTemperatureField);

	// Output enable/disable per monitor

	fOutputMenu = new BPopUpMenu("Outputs", true, true);
	fOutputField = new BMenuField("OutputMenu",
		B_TRANSLATE("Outputs:"), fOutputMenu);
	fOutputField->SetAlignment(B_ALIGN_RIGHT);
	_UpdateOutputMenu();

	// Profile save / delete

	fProfileMenu = new BPopUpMenu("Profiles", true, true);
	fProfileField = new BMenuField("ProfileMenu",
		B_TRANSLATE("Profile:"), fProfileMenu);
	fProfileField->SetAlignment(B_ALIGN_RIGHT);
	_UpdateProfileMenu();

	fSaveProfileButton = new BButton("SaveProfileButton",
		B_TRANSLATE("Save" B_UTF8_ELLIPSIS),
		new BMessage(BUTTON_PROFILE_SAVE_MSG));

	fDeleteProfileButton = new BButton("DeleteProfileButton",
		B_TRANSLATE("Delete"),
		new BMessage(BUTTON_PROFILE_DELETE_MSG));
	fDeleteProfileButton->SetEnabled(false);

	BLayoutBuilder::Group<>(outerControlsView)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fOutputField->CreateLabelLayoutItem(), 0, 9)
			.Add(fOutputField->CreateMenuBarLayoutItem(), 1, 9)
			.Add(fProfileField->CreateLabelLayoutItem(), 0, 10)
			.Add(fProfileField->CreateMenuBarLayoutItem(), 1, 10)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fSaveProfileButton)
			.Add(fDeleteProfileButton)
			.AddGlue();

	// TODO: we don't support getting the screen's preferred settings
	/* fDefaultsButton = new BButton(buttonRect, "DefaultsButton", "Defaults",
		new BMessage(BUTTON_DEFAULTS_MSG));*/

	fApplyButton = new BButton("ApplyButton", B_TRANSLATE("Apply"),
		new BMessage(BUTTON_APPLY_MSG));
	fApplyButton->SetEnabled(false);
	BLayoutBuilder::Group<>(outerControlsView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton);

	fRevertButton = new BButton("RevertButton", B_TRANSLATE("Revert"),
		new BMessage(BUTTON_REVERT_MSG));
	fRevertButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.AddGroup(B_VERTICAL, 0, 1)
				.AddStrut(floorf(controlsBox->TopBorderOffset()
					- fScreenBox->TopBorderOffset()))
				.Add(fScreenBox)
				.Add(workspacesView)
				.End()
			.AddGroup(B_VERTICAL, 0, 1)
				.Add(controlsBox, 2)
				.End()
			.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRevertButton)
			.AddGlue()
			.End()
		.SetInsets(B_USE_WINDOW_SPACING);

	_UpdateControls();
	_UpdateMonitor();

	MoveOnScreen();
}


ScreenWindow::~ScreenWindow()
{
	delete fSettings;
}


bool
ScreenWindow::QuitRequested()
{
	fSettings->SetWindowFrame(Frame());

	// Write mode of workspace 0 (the boot workspace) to the vesa settings file
	screen_mode vesaMode;
	if (fBootWorkspaceApplied && fScreenMode.Get(vesaMode, 0) == B_OK) {
		status_t status = _WriteVesaModeFile(vesaMode);
		if (status < B_OK) {
			BString warning = B_TRANSLATE("Could not write VESA mode settings"
				" file:\n\t");
			warning << strerror(status);
			BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
				warning.String(), B_TRANSLATE("OK"), NULL,
				NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


/*!	Update resolution list according to combine mode
	(some resolutions may not be combinable due to memory restrictions).
*/
void
ScreenWindow::_CheckResolutionMenu()
{
	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetEnabled(false);

	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.combine != fSelected.combine)
			continue;

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		BMenuItem *item = fResolutionMenu->FindItem(name.String());
		if (item != NULL)
			item->SetEnabled(true);
	}
}


/*!	Update color and refresh options according to current mode
	(a color space is made active if there is any mode with
	given resolution and this colour space; same applies for
	refresh rate, though "Other…" is always possible)
*/
void
ScreenWindow::_CheckColorMenu()
{
	int32 supportsAnything = false;
	int32 index = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		bool supported = false;

		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			screen_mode mode = fScreenMode.ModeAt(j);

			if (fSelected.width == mode.width
				&& fSelected.height == mode.height
				&& kColorSpaces[i].space == mode.space
				&& fSelected.combine == mode.combine) {
				supportsAnything = true;
				supported = true;
				break;
			}
		}

		BMenuItem* item = fColorsMenu->ItemAt(index++);
		if (item)
			item->SetEnabled(supported);
	}

	fColorsField->SetEnabled(supportsAnything);

	if (!supportsAnything)
		return;

	// Make sure a valid item is selected

	BMenuItem* item = fColorsMenu->FindMarked();
	bool changed = false;

	if (item != fUserSelectedColorSpace) {
		if (fUserSelectedColorSpace != NULL
			&& fUserSelectedColorSpace->IsEnabled()) {
			fUserSelectedColorSpace->SetMarked(true);
			item = fUserSelectedColorSpace;
			changed = true;
		}
	}
	if (item != NULL && !item->IsEnabled()) {
		// find the next best item
		int32 index = fColorsMenu->IndexOf(item);
		bool found = false;

		for (int32 i = index + 1; i < fColorsMenu->CountItems(); i++) {
			item = fColorsMenu->ItemAt(i);
			if (item->IsEnabled()) {
				found = true;
				break;
			}
		}
		if (!found) {
			// search backwards as well
			for (int32 i = index - 1; i >= 0; i--) {
				item = fColorsMenu->ItemAt(i);
				if (item->IsEnabled())
					break;
			}
		}

		item->SetMarked(true);
		changed = true;
	}

	if (changed) {
		// Update selected space

		BMessage* message = item->Message();
		int32 space;
		if (message->FindInt32("space", &space) == B_OK) {
			fSelected.space = (color_space)space;
			_UpdateColorLabel();
		}
	}
}


/*!	Enable/disable refresh options according to current mode. */
void
ScreenWindow::_CheckRefreshMenu()
{
	float min, max;
	if (fScreenMode.GetRefreshLimits(fSelected, min, max) != B_OK || min == max)
		return;

	for (int32 i = fRefreshMenu->CountItems(); i-- > 0;) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		BMessage* message = item->Message();
		float refresh;
		if (message != NULL && message->FindFloat("refresh", &refresh) == B_OK)
			item->SetEnabled(refresh >= min && refresh <= max);
	}
}


/*!	Activate appropriate menu item according to selected refresh rate */
void
ScreenWindow::_UpdateRefreshControl()
{
	if (isnan(fSelected.refresh)) {
		fRefreshMenu->SetEnabled(false);
		fOtherRefresh->SetLabel(B_TRANSLATE("Unknown"));
		fOtherRefresh->SetMarked(true);
		return;
	} else {
		fRefreshMenu->SetEnabled(true);
	}

	for (int32 i = 0; i < fRefreshMenu->CountItems(); i++) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		if (item->Message()->FindFloat("refresh") == fSelected.refresh) {
			item->SetMarked(true);
			// "Other" items only contains a refresh rate when active
			if (fOtherRefresh != NULL)
				fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
			return;
		}
	}
	
	// this is a non-standard refresh rate
	if (fOtherRefresh != NULL) {
		fOtherRefresh->Message()->ReplaceFloat("refresh", fSelected.refresh);
		fOtherRefresh->SetMarked(true);

		BString string;
		refresh_rate_to_string(fSelected.refresh, string);
		fRefreshMenu->Superitem()->SetLabel(string.String());

		string.Append(B_TRANSLATE("/other" B_UTF8_ELLIPSIS));
		fOtherRefresh->SetLabel(string.String());
	}
}


void
ScreenWindow::_UpdateMonitorView()
{
	BMessage updateMessage(UPDATE_DESKTOP_MSG);
	updateMessage.AddInt32("width", fSelected.width);
	updateMessage.AddInt32("height", fSelected.height);

	PostMessage(&updateMessage, fMonitorView);
}


void
ScreenWindow::_MarkTemperaturePreset(float kelvin)
{
	if (fTemperatureMenu == NULL)
		return;

	BMenuItem* best = NULL;
	float bestDelta = 0.0f;
	for (int32 i = 0; i < fTemperatureMenu->CountItems(); i++) {
		BMenuItem* item = fTemperatureMenu->ItemAt(i);
		float preset;
		if (item->Message()->FindFloat("kelvin", &preset) != B_OK)
			continue;
		float delta = fabsf(preset - kelvin);
		if (best == NULL || delta < bestDelta) {
			best = item;
			bestDelta = delta;
		}
	}
	// One mark only: nearest preset, even for a stored non-preset value.
	for (int32 i = 0; i < fTemperatureMenu->CountItems(); i++)
		fTemperatureMenu->ItemAt(i)->SetMarked(
			fTemperatureMenu->ItemAt(i) == best);
}


void
ScreenWindow::_UpdateTemperatureControls()
{
	BScreen screen(this);
	float current = 6500.0f;
	bool supported = screen.GetTemperature(&current) == B_OK;

	// Hide()/Show() nest (fShowLevel); only toggle on a real change.
	if (supported != fTemperatureSupported) {
		if (supported)
			fTemperatureField->Show();
		else
			fTemperatureField->Hide();

		fTemperatureSupported = supported;
	}

	if (supported) {
		fTemperatureField->SetToolTip("");
		// Keep the applied value; only re-mark the menu.
		_MarkTemperaturePreset(fTemperature);
	} else
		fTemperatureField->SetToolTip(B_TRANSLATE(
			"This display does not support colour temperature."));
}


void
ScreenWindow::_UpdateControls()
{
	_UpdateWorkspaceButtons();
	_UpdateTemperatureControls();

	BMenuItem* item = fSwapDisplaysMenu->ItemAt((int32)fSelected.swap_displays);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	item = fUseLaptopPanelMenu->ItemAt((int32)fSelected.use_laptop_panel);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	for (int32 i = 0; i < fTVStandardMenu->CountItems(); i++) {
		item = fTVStandardMenu->ItemAt(i);

		uint32 tvStandard;
		item->Message()->FindInt32("tv_standard", (int32 *)&tvStandard);
		if (tvStandard == fSelected.tv_standard) {
			if (!item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	_CheckResolutionMenu();
	_CheckColorMenu();
	_CheckRefreshMenu();

	BString string;
	resolution_to_string(fSelected, string);
	item = fResolutionMenu->FindItem(string.String());

	if (item != NULL) {
		if (!item->IsMarked())
			item->SetMarked(true);
	} else {
		// this is bad luck - if mode has been set via screen references,
		// this case cannot occur; there are three possible solutions:
		// 1. add a new resolution to list
		//    - we had to remove it as soon as a "valid" one is selected
		//    - we don't know which frequencies/bit depths are supported
		//    - as long as we haven't the GMT formula to create
		//      parameters for any resolution given, we cannot
		//      really set current mode - it's just not in the list
		// 2. choose nearest resolution
		//    - probably a good idea, but implies coding and testing
		// 3. choose lowest resolution
		//    - do you really think we are so lazy? yes, we are
		item = fResolutionMenu->ItemAt(0);
		if (item)
			item->SetMarked(true);

		// okay - at least we set menu label to active resolution
		fResolutionMenu->Superitem()->SetLabel(string.String());
	}

	// mark active combine mode
	for (int32 i = 0; i < kCombineModeCount; i++) {
		if (kCombineModes[i].mode == fSelected.combine) {
			item = fCombineMenu->ItemAt(i);
			if (item != NULL && !item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	item = fColorsMenu->ItemAt(0);

	for (int32 i = 0, index = 0; i <  kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		if (kColorSpaces[i].space == fSelected.space) {
			item = fColorsMenu->ItemAt(index);
			break;
		}

		index++;
	}

	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	_UpdateColorLabel();
	_UpdateMonitorView();
	_UpdateRefreshControl();

	_CheckApplyEnabled();
}


/*! Reflect active mode in chosen settings */
// Matched on the message value, not the item index: reflection has no Auto
// entry, so it carries none of rotation's index offset.
void
ScreenWindow::_MarkReflectionItem(int32 reflection)
{
	for (int32 i = 0; i < fReflectionMenu->CountItems(); i++) {
		BMenuItem* item = fReflectionMenu->ItemAt(i);
		int32 itemReflection;
		if (item->Message()->FindInt32("reflection", &itemReflection) == B_OK
			&& itemReflection == reflection) {
			item->SetMarked(true);
			return;
		}
	}
}


void
ScreenWindow::_UpdateActiveMode()
{
	_UpdateActiveMode(current_workspace());
}


void
ScreenWindow::_UpdateActiveMode(int32 workspace)
{
	// Usually, this function gets called after a mode
	// has been set manually; still, as the graphics driver
	// is free to fiddle with mode passed, we better ask
	// what kind of mode we actually got
	if (fScreenMode.Get(fActive, workspace) == B_OK) {
		fSelected = fActive;

		_UpdateMonitor();
		_BuildSupportedColorSpaces();
		_UpdateControls();
	}
}


void
ScreenWindow::_UpdateWorkspaceButtons()
{
	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	// Set the max values enabling/disabling the up/down arrows

	if (rows == 1)
		fColumnsControl->SetMaxValue(32);
	else if (rows == 2)
		fColumnsControl->SetMaxValue(16);
	else if (rows <= 4)
		fColumnsControl->SetMaxValue(8);
	else if (rows <= 8)
		fColumnsControl->SetMaxValue(4);
	else if (rows <= 16)
		fColumnsControl->SetMaxValue(2);
	else if (rows <= 32)
		fColumnsControl->SetMaxValue(1);

	if (columns == 1)
		fRowsControl->SetMaxValue(32);
	else if (columns == 2)
		fRowsControl->SetMaxValue(16);
	else if (columns <= 4)
		fRowsControl->SetMaxValue(8);
	else if (columns <= 8)
		fRowsControl->SetMaxValue(4);
	else if (columns <= 16)
		fRowsControl->SetMaxValue(2);
	else if (columns <= 32)
		fRowsControl->SetMaxValue(1);
}


void
ScreenWindow::ScreenChanged(BRect frame, color_space mode)
{
	// External size/colour change: re-read the live mode. Do not treat it
	// as a user edit, so Apply/Revert stay quiet.
	fModified = false;
	fBootWorkspaceApplied = false;

	// Adopt the new external state as the revert baseline.
	_UpdateOriginal();
	_UpdateActiveMode();

	// Keep the preflet fully on the new screen.
	const BRect windowFrame = Frame();
	if (windowFrame.left < frame.left || windowFrame.top < frame.top
		|| windowFrame.right > frame.right
		|| windowFrame.bottom > frame.bottom) {
		MoveTo(frame.left + (frame.Width() - windowFrame.Width()) / 2.0f,
			frame.top + (frame.Height() - windowFrame.Height()) / 2.0f);
	}
}


void
ScreenWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fScreenMode.GetOriginalMode(fOriginal, workspace) == B_OK) {
		_UpdateActiveMode(workspace);

		BMessage message(UPDATE_DESKTOP_COLOR_MSG);
		PostMessage(&message, fMonitorView);
	}
}


void
ScreenWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case WORKSPACE_CHECK_MSG:
			_CheckApplyEnabled();
			break;

		case kMsgWorkspaceColumnsChanged:
		{
			uint32 newColumns = (uint32)fColumnsControl->Value();

			uint32 rows;
			BPrivate::get_workspaces_layout(NULL, &rows);
			BPrivate::set_workspaces_layout(newColumns, rows);

			_UpdateWorkspaceButtons();
			fRowsControl->SetValue(rows);
				// enables/disables up/down arrows
			_CheckApplyEnabled();

			break;
		}

		case kMsgWorkspaceRowsChanged:
		{
			uint32 newRows = (uint32)fRowsControl->Value();

			uint32 columns;
			BPrivate::get_workspaces_layout(&columns, NULL);
			BPrivate::set_workspaces_layout(columns, newRows);

			_UpdateWorkspaceButtons();
			fColumnsControl->SetValue(columns);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			break;
		}

		case POP_RESOLUTION_MSG:
		{
			message->FindInt32("width", &fSelected.width);
			message->FindInt32("height", &fSelected.height);

			_CheckColorMenu();
			_CheckRefreshMenu();

			_UpdateMonitorView();
			_UpdateRefreshControl();

			_CheckApplyEnabled();
			break;
		}

		case POP_COLORS_MSG:
		{
			int32 space;
			if (message->FindInt32("space", &space) != B_OK)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fColorsMenu->ItemAt(index) != NULL)
				fUserSelectedColorSpace = fColorsMenu->ItemAt(index);

			fSelected.space = (color_space)space;
			_UpdateColorLabel();

			_CheckApplyEnabled();
			break;
		}

		case POP_REFRESH_MSG:
		{
			message->FindFloat("refresh", &fSelected.refresh);
			fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
				// revert "Other…" label - it might have a refresh rate prefix

			_CheckApplyEnabled();
			break;
		}

		case POP_OTHER_REFRESH_MSG:
		{
			// make sure menu shows something useful
			_UpdateRefreshControl();

			float min = 0, max = 999;
			fScreenMode.GetRefreshLimits(fSelected, min, max);
			if (min < gMinRefresh)
				min = gMinRefresh;
			if (max > gMaxRefresh)
				max = gMaxRefresh;

			monitor_info info;
			if (fScreenMode.GetMonitorInfo(info) == B_OK) {
				min = max_c(info.min_vertical_frequency, min);
				max = min_c(info.max_vertical_frequency, max);
			}

			RefreshWindow *fRefreshWindow = new RefreshWindow(
				fRefreshField->ConvertToScreen(B_ORIGIN), fSelected.refresh,
				min, max);
			fRefreshWindow->Show();
			break;
		}

		case SET_CUSTOM_REFRESH_MSG:
		{
			// user pressed "done" in "Other…" refresh dialog;
			// select the refresh rate chosen
			message->FindFloat("refresh", &fSelected.refresh);

			_UpdateRefreshControl();
			_CheckApplyEnabled();
			break;
		}

		case POP_ROTATION_MSG:
		{
			int32 rotation;
			if (message->FindInt32("rotation", &rotation) != B_OK)
				break;

			// Applied at once rather than on Apply: rotation does not go
			// through the mode list, so there is nothing to negotiate.
			if (fScreenMode.SetRotation(rotation) != B_OK) {
				BAlert* alert = new BAlert(B_TRANSLATE("Rotation"),
					B_TRANSLATE("This graphics backend cannot rotate the "
						"screen."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);

				BMenuItem* item = fRotationMenu->ItemAt(fOriginalRotation + 1);
				if (item != NULL)
					item->SetMarked(true);
			}
			break;
		}

		case POP_REFLECTION_MSG:
		{
			int32 reflection;
			if (message->FindInt32("reflection", &reflection) != B_OK)
				break;

			// Applied at once rather than on Apply: reflection does not go
			// through the mode list, so there is nothing to negotiate.
			if (fScreenMode.SetReflection(reflection) != B_OK) {
				BAlert* alert = new BAlert(B_TRANSLATE("Reflection"),
					B_TRANSLATE("This graphics backend cannot reflect the "
						"screen."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);

				_MarkReflectionItem(fOriginalReflection);
			}
			break;
		}

		case POP_COMBINE_DISPLAYS_MSG:
		{
			// new combine mode has bee chosen
			int32 mode;
			if (message->FindInt32("mode", &mode) == B_OK)
				fSelected.combine = (combine_mode)mode;

			_CheckResolutionMenu();
			_CheckApplyEnabled();
			break;
		}

		case POP_SWAP_DISPLAYS_MSG:
			message->FindBool("swap", &fSelected.swap_displays);
			_CheckApplyEnabled();
			break;

		case POP_USE_LAPTOP_PANEL_MSG:
			message->FindBool("use", &fSelected.use_laptop_panel);
			_CheckApplyEnabled();
			break;

		case POP_TV_STANDARD_MSG:
			message->FindInt32("tv_standard", (int32 *)&fSelected.tv_standard);
			_CheckApplyEnabled();
			break;

		case BUTTON_LAUNCH_BACKGROUNDS_MSG:
			if (be_roster->Launch(kBackgroundsSignature) == B_ALREADY_RUNNING) {
				app_info info;
				be_roster->GetAppInfo(kBackgroundsSignature, &info);
				be_roster->ActivateApp(info.team);
			}
			break;

		case BUTTON_DEFAULTS_MSG:
		{
			// TODO: get preferred settings of screen
			fSelected.width = 640;
			fSelected.height = 480;
			fSelected.space = B_CMAP8;
			fSelected.refresh = 60.0;
			fSelected.combine = kCombineDisable;
			fSelected.swap_displays = false;
			fSelected.use_laptop_panel = false;
			fSelected.tv_standard = 0;

			// TODO: workspace defaults

			_UpdateControls();
			break;
		}

		case BUTTON_UNDO_MSG:
			fUndoScreenMode.Revert();
			_UpdateActiveMode();
			break;

		case BUTTON_REVERT_MSG:
		{
			fModified = false;
			fBootWorkspaceApplied = false;

			// ScreenMode::Revert() assumes that we first set the correct
			// number of workspaces

			BPrivate::set_workspaces_layout(fOriginalWorkspacesColumns,
				fOriginalWorkspacesRows);
			_UpdateWorkspaceButtons();

			fScreenMode.Revert();

			BScreen screen(this);
			screen.SetBrightness(fOriginalBrightness);
			fBrightnessSlider->SetValue(fOriginalBrightness * 255);

			if (fTemperatureSupported) {
				screen.SetTemperature(fOriginalTemperature);
				fTemperature = fOriginalTemperature;
				_MarkTemperaturePreset(fTemperature);
			}

			fScreenMode.SetRotation(fOriginalRotation);
			BMenuItem* rotationItem
				= fRotationMenu->ItemAt(fOriginalRotation + 1);
			if (rotationItem != NULL)
				rotationItem->SetMarked(true);

			fScreenMode.SetReflection(fOriginalReflection);
			_MarkReflectionItem(fOriginalReflection);

			_UpdateActiveMode();
			break;
		}

		case BUTTON_APPLY_MSG:
			_Apply();
			break;

		case MAKE_INITIAL_MSG:
			// user pressed "keep" in confirmation dialog
			fModified = true;
			_UpdateActiveMode();
			break;

		case UPDATE_DESKTOP_COLOR_MSG:
			PostMessage(message, fMonitorView);
			break;

		case SLIDER_BRIGHTNESS_MSG:
		{
			BScreen screen(this);
			screen.SetBrightness(message->FindInt32("be:value") / 255.f);
			_CheckApplyEnabled();
			break;
		}

		case POP_TEMPERATURE_MSG:
		{
			if (!fTemperatureSupported)
				break;

			float kelvin;
			if (message->FindFloat("kelvin", &kelvin) != B_OK)
				break;

			fTemperature = kelvin;
			BScreen screen(this);
			screen.SetTemperature(fTemperature);
			_MarkTemperaturePreset(fTemperature);
			break;
		}

		case POP_OUTPUT_TOGGLE_MSG:
		{
			// Output enable/disable toggled
			int32 outputID;
			bool enabled;
			if (message->FindInt32("output_id", &outputID) == B_OK
				&& message->FindBool("enabled", &enabled) == B_OK) {
				screen_id sid;
				sid.id = outputID;
				BScreen screen(sid);
				if (screen.IsValid()) {
					screen.SetDPMS(enabled ? B_DPMS_ON : B_DPMS_OFF);
				}
			}
			break;
		}

		case POP_PROFILE_SELECT_MSG:
		{
			const char* profileName;
			if (message->FindString("profile_name", &profileName) == B_OK) {
				fDeleteProfileButton->SetEnabled(true);
				_ApplyProfile(profileName);
			}
			break;
		}

		case BUTTON_PROFILE_SAVE_MSG:
		{
			// Show a simple alert asking for a profile name
			BAlert* alert = new BAlert(B_TRANSLATE("Save Profile"),
				B_TRANSLATE("Enter the profile name in the window title "
					"and press OK to save."),
				B_TRANSLATE("OK"), B_TRANSLATE("Cancel"), NULL,
				B_WIDTH_AS_USUAL, B_INFO_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			int32 result = alert->Go();
			if (result == 0) {
				// Save with a default name based on resolution
				BString profileName;
				profileName.SetToFormat("%" B_PRId32 "x%" B_PRId32,
					fSelected.width, fSelected.height);
				_SaveProfile(profileName.String());
				_UpdateProfileMenu();
			}
			break;
		}

		case BUTTON_PROFILE_DELETE_MSG:
		{
			BMenuItem* marked = fProfileMenu->FindMarked();
			if (marked != NULL) {
				const char* profileName;
				if (marked->Message()->FindString("profile_name",
						&profileName) == B_OK) {
					_DeleteProfile(profileName);
					_UpdateProfileMenu();
					fDeleteProfileButton->SetEnabled(false);
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


status_t
ScreenWindow::_WriteVesaModeFile(const screen_mode& mode) const
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status < B_OK)
		return status;

	path.Append("kernel/drivers");
	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	path.Append("vesa");
	BFile file;
	status = file.SetTo(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | B_ERASE_FILE);
	if (status < B_OK)
		return status;

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "mode %" B_PRId32 " %" B_PRId32 " %"
		B_PRId32 "\n", mode.width, mode.height, mode.BitsPerPixel());

	ssize_t bytesWritten = file.Write(buffer, strlen(buffer));
	if (bytesWritten < B_OK)
		return bytesWritten;

	return B_OK;
}


void
ScreenWindow::_BuildSupportedColorSpaces()
{
	fSupportedColorSpaces = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			if (fScreenMode.ModeAt(j).space == kColorSpaces[i].space) {
				fSupportedColorSpaces |= 1 << i;
				break;
			}
		}
	}
}


void
ScreenWindow::_CheckApplyEnabled()
{
	bool applyEnabled = true;

	if (fSelected == fActive) {
		applyEnabled = false;
		if (fAllWorkspacesItem->IsMarked()) {
			screen_mode screenMode;
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				fScreenMode.Get(screenMode, i);
				if (screenMode != fSelected) {
					applyEnabled = true;
					break;
				}
			}
		}
	}

	fApplyButton->SetEnabled(applyEnabled);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	BScreen screen(this);
	float brightness = -1;
	screen.GetBrightness(&brightness);

	fRevertButton->SetEnabled(columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| fSelected != fOriginal);
}


void
ScreenWindow::_UpdateOriginal()
{
	BPrivate::get_workspaces_layout(&fOriginalWorkspacesColumns,
		&fOriginalWorkspacesRows);

	fScreenMode.Get(fOriginal);
	fScreenMode.UpdateOriginalModes();
}


void
ScreenWindow::_UpdateMonitor()
{
	// Mode size on the box; connector + EDID identity under the picture
	// so the two captions do not repeat each other.
	char text[128];
	snprintf(text, sizeof(text), "%dx%d", fSelected.width, fSelected.height);
	fScreenBox->SetLabel(text);

	monitor_info info;
	status_t status = fScreenMode.GetMonitorInfo(info);

	BString caption;
	BString connector;
	if (fScreenMode.GetConnectorName(connector) == B_OK
			&& connector.Length() > 0)
		caption = connector;

	if (status == B_OK && (info.vendor[0] || info.name[0])) {
		BString display;
		if (info.vendor[0] && info.name[0])
			display.SetToFormat("%s %s", info.vendor, info.name);
		else
			display = info.vendor[0] ? info.vendor : info.name;
		if (caption.Length() > 0)
			caption << " " << display;
		else
			caption = display;
	}

	if (caption.Length() > 0) {
		fDeviceInfo->SetText(caption);
	} else {
		// No connector/EDID path (accelerant backends): keep the GPU name.
		accelerant_device_info deviceInfo;

		if (fScreenMode.GetDeviceInfo(deviceInfo) == B_OK) {
			BString deviceString;

			if (deviceInfo.name[0] && deviceInfo.chipset[0]) {
				deviceString.SetToFormat("%s (%s)", deviceInfo.name,
					deviceInfo.chipset);
			} else if (deviceInfo.name[0] || deviceInfo.chipset[0]) {
				deviceString
					= deviceInfo.name[0] ? deviceInfo.name : deviceInfo.chipset;
			}

			fDeviceInfo->SetText(deviceString);
		} else
			fDeviceInfo->SetText("");
	}

	// EDID ranges still go to the monitor tooltip.

	char tooltip[512];
	size_t length = 0;
	tooltip[0] = 0;

	if (status == B_OK) {
		if (info.min_horizontal_frequency != 0
			&& info.min_vertical_frequency != 0
			&& info.max_pixel_clock != 0) {
			length = snprintf(tooltip, sizeof(tooltip),
				B_TRANSLATE("Horizonal frequency:\t%lu - %lu kHz\n"
					"Vertical frequency:\t%lu - %lu Hz\n\n"
					"Maximum pixel clock:\t%g MHz"),
				(long unsigned)info.min_horizontal_frequency,
				(long unsigned)info.max_horizontal_frequency,
				(long unsigned)info.min_vertical_frequency,
				(long unsigned)info.max_vertical_frequency,
				info.max_pixel_clock / 1000.0);
		}
		if (info.serial_number[0] && length < sizeof(tooltip)) {
			if (length > 0) {
				tooltip[length++] = '\n';
				tooltip[length++] = '\n';
				tooltip[length] = '\0';
			}
			length += snprintf(tooltip + length, sizeof(tooltip) - length,
				B_TRANSLATE("Serial no.: %s"), info.serial_number);
			if (info.produced.week != 0 && info.produced.year != 0
				&& length < sizeof(tooltip)) {
				length += snprintf(tooltip + length, sizeof(tooltip) - length,
					" (%u/%u)", info.produced.week, info.produced.year);
			}
		}
	}

	if (tooltip[0])
		fMonitorView->SetToolTip(tooltip);
}


void
ScreenWindow::_UpdateColorLabel()
{
	BString string;
	string << fSelected.BitsPerPixel() << " " << B_TRANSLATE("bits/pixel");
	fColorsMenu->Superitem()->SetLabel(string.String());
}


void
ScreenWindow::_Apply()
{
	// make checkpoint, so we can undo these changes
	fUndoScreenMode.UpdateOriginalModes();

	status_t status = fScreenMode.Set(fSelected);
	if (status == B_OK) {
		// use the mode that has eventually been set and
		// thus we know to be working; it can differ from
		// the mode selected by user due to hardware limitation
		display_mode newMode;
		BScreen screen(this);
		screen.GetMode(&newMode);

		if (fAllWorkspacesItem->IsMarked()) {
			int32 originatingWorkspace = current_workspace();
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				if (i != originatingWorkspace)
					screen.SetMode(i, &newMode, true);
			}
			fBootWorkspaceApplied = true;
		} else {
			if (current_workspace() == 0)
				fBootWorkspaceApplied = true;
		}

		fActive = fSelected;

		// TODO: only show alert when this is an unknown mode
		BAlert* window = new AlertWindow(this);
		window->Go(NULL);
	} else {
		char message[256];
		snprintf(message, sizeof(message),
			B_TRANSLATE("The screen mode could not be set:\n\t%s\n"),
			screen_errors(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Warning"), message,
			B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
}


void
ScreenWindow::_UpdateOutputMenu()
{
	fOutputMenu->RemoveItems(0, fOutputMenu->CountItems(), true);

	// Enumerate available screen outputs using BPrivateScreen
	int32 screenID = -1;
	BPrivate::BPrivateScreen* ps = BPrivate::BPrivateScreen::Get(this);
	if (ps == NULL) {
		// Fallback: add the main screen only
		BMessage* msg = new BMessage(POP_OUTPUT_TOGGLE_MSG);
		msg->AddInt32("output_id", 0);
		msg->AddBool("enabled", true);
		fOutputMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Main display"), msg));
		fOutputMenu->ItemAt(0)->SetMarked(true);
		return;
	}

	// Get the current screen's ID first
	screenID = ps->ID();
	BPrivate::BPrivateScreen::Put(ps);

	// Enumerate all screens
	int32 id = 0;
	int32 count = 0;

	// Enumerate screens by probing IDs 0..15
	for (id = 0; id < 16; id++) {
		screen_id sid;
		sid.id = id;
		BScreen screen(sid);
		if (screen.IsValid()) {
			BString name;
			accelerant_device_info info;
			if (screen.GetDeviceInfo(&info) == B_OK && info.name[0]) {
				name.SetToFormat("%" B_PRId32 ": %s", id, info.name);
			} else {
				name.SetToFormat(B_TRANSLATE("Display %" B_PRId32), id);
			}

			BMessage* msg = new BMessage(POP_OUTPUT_TOGGLE_MSG);
			msg->AddInt32("output_id", id);
			msg->AddBool("enabled", true);
			fOutputMenu->AddItem(new BMenuItem(name.String(), msg));

			if (id == screenID) {
				fOutputMenu->ItemAt(count)->SetMarked(true);
			}
			count++;
		}
	}

	if (count == 0) {
		BMessage* msg = new BMessage(POP_OUTPUT_TOGGLE_MSG);
		msg->AddInt32("output_id", 0);
		msg->AddBool("enabled", true);
		fOutputMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Main display"), msg));
		fOutputMenu->ItemAt(0)->SetMarked(true);
	}
}


static BPath
_profileDirectory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return path;

	path.Append("screen_profiles");
	create_directory(path.Path(), 0755);
	return path;
}


void
ScreenWindow::_UpdateProfileMenu()
{
	// Remove all existing items
	fProfileMenu->RemoveItems(0, fProfileMenu->CountItems(), true);

	BPath dir = _profileDirectory();
	if (dir.InitCheck() < B_OK) {
		fProfileMenu->AddItem(new BMenuItem(
			B_TRANSLATE("No profiles"), new BMessage()));
		return;
	}

	BDirectory directory(dir.Path());
	if (directory.InitCheck() < B_OK) {
		fProfileMenu->AddItem(new BMenuItem(
			B_TRANSLATE("No profiles"), new BMessage()));
		return;
	}

	BEntry entry;
	int32 count = 0;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath entryPath;
		if (entry.GetPath(&entryPath) != B_OK)
			continue;

		// Only include .screenprofile files
		const char* name = entryPath.Leaf();
		const char* ext = strrchr(name, '.');
		if (ext == NULL || strcmp(ext, ".screenprofile") != 0)
			continue;

		// Strip extension for display
		BString displayName(name);
		int32 dotPos = displayName.FindLast('.');
		if (dotPos >= 0)
			displayName.Truncate(dotPos);

		BMessage* msg = new BMessage(POP_PROFILE_SELECT_MSG);
		msg->AddString("profile_name", displayName.String());
		fProfileMenu->AddItem(new BMenuItem(displayName.String(), msg));
		count++;
	}

	if (count == 0) {
		fProfileMenu->AddItem(new BMenuItem(
			B_TRANSLATE("No profiles"), new BMessage()));
	}
}


status_t
ScreenWindow::_SaveProfile(const char* name)
{
	BPath dir = _profileDirectory();
	if (dir.InitCheck() < B_OK)
		return dir.InitCheck();

	BString fileName(name);
	fileName << ".screenprofile";
	dir.Append(fileName.String());

	BFile file(dir.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() < B_OK)
		return file.InitCheck();

	// Write the screen_mode fields as a simple text format
	char buffer[512];
	snprintf(buffer, sizeof(buffer),
		"width %" B_PRId32 "\n"
		"height %" B_PRId32 "\n"
		"space %" B_PRId32 "\n"
		"refresh %g\n"
		"combine %" B_PRId32 "\n"
		"swap_displays %d\n"
		"use_laptop_panel %d\n"
		"tv_standard %" B_PRId32 "\n",
		fSelected.width,
		fSelected.height,
		(int32)fSelected.space,
		fSelected.refresh,
		(int32)fSelected.combine,
		(int32)fSelected.swap_displays,
		(int32)fSelected.use_laptop_panel,
		fSelected.tv_standard);

	ssize_t written = file.Write(buffer, strlen(buffer));
	if (written < (ssize_t)strlen(buffer))
		return B_ERROR;

	return B_OK;
}


status_t
ScreenWindow::_DeleteProfile(const char* name)
{
	BPath dir = _profileDirectory();
	if (dir.InitCheck() < B_OK)
		return dir.InitCheck();

	BString fileName(name);
	fileName << ".screenprofile";
	dir.Append(fileName.String());

	return BEntry(dir.Path()).Remove();
}


status_t
ScreenWindow::_ApplyProfile(const char* name)
{
	BPath dir = _profileDirectory();
	if (dir.InitCheck() < B_OK)
		return dir.InitCheck();

	BString fileName(name);
	fileName << ".screenprofile";
	dir.Append(fileName.String());

	BFile file(dir.Path(), B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return file.InitCheck();

	// Read the profile
	char buffer[512];
	ssize_t bytesRead = file.Read(buffer, sizeof(buffer) - 1);
	if (bytesRead <= 0)
		return B_ERROR;
	buffer[bytesRead] = '\0';

	// Parse the simple text format
	screen_mode profile;
	memset(&profile, 0, sizeof(profile));
	profile.combine = kCombineDisable;

	const char* p = buffer;
	char key[64];

	while (p != NULL && *p) {
		// Skip whitespace
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;

		// Read key
		const char* keyStart = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		int32 keyLen = p - keyStart;
		if (keyLen >= (int32)sizeof(key))
			break;
		memcpy(key, keyStart, keyLen);
		key[keyLen] = '\0';

		// Skip space
		while (*p == ' ' || *p == '\t')
			p++;

		// Read value
		const char* valStart = p;
		while (*p && *p != '\n')
			p++;

		if (strcmp(key, "width") == 0) {
			profile.width = atol(valStart);
		} else if (strcmp(key, "height") == 0) {
			profile.height = atol(valStart);
		} else if (strcmp(key, "space") == 0) {
			profile.space = (color_space)atol(valStart);
		} else if (strcmp(key, "refresh") == 0) {
			profile.refresh = atof(valStart);
		} else if (strcmp(key, "combine") == 0) {
			profile.combine = (combine_mode)atol(valStart);
		} else if (strcmp(key, "swap_displays") == 0) {
			profile.swap_displays = atoi(valStart) != 0;
		} else if (strcmp(key, "use_laptop_panel") == 0) {
			profile.use_laptop_panel = atoi(valStart) != 0;
		} else if (strcmp(key, "tv_standard") == 0) {
			profile.tv_standard = atol(valStart);
		}

		// Move past newline
		while (*p == '\n')
			p++;
	}

	// Apply the profile
	fSelected = profile;
	_UpdateControls();
	_CheckApplyEnabled();

	return B_OK;
}
