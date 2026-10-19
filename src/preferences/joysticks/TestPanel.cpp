/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "TestPanel.h"

#include "CalibrationSettings.h"
#include "RumbleTester.h"

#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <Joystick.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Joysticks"


static const uint32 kMsgRumbleTest = 'rmbT';

static const float kRowHeight = 20.0f;
static const float kPillWidth = 52.0f;
static const float kPillHeight = 22.0f;
static const float kPillGap = 6.0f;

static const char* kHatNames[9] = {
	"Center", "N", "NE", "E", "SE", "S", "SW", "W", "NW"
};


// ---- AxisMeterView: one bar per axis, centered at rest ----


class AxisMeterView : public BView {
public:
								AxisMeterView();

			void				SetAxes(const std::vector<BString>& names);
			void				SetValues(const std::vector<float>& values);

	virtual	void				Draw();

private:
			std::vector<BString>	fNames;
			std::vector<float>		fValues;
};


AxisMeterView::AxisMeterView()
	:
	BView("axis meters", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
AxisMeterView::SetAxes(const std::vector<BString>& names)
{
	fNames = names;
	fValues.assign(names.size(), 0.0f);

	float height = std::max((float)names.size() * kRowHeight, kRowHeight);
	SetExplicitMinSize(BSize(280, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNSET, height));
	SetExplicitPreferredSize(BSize(340, height));
	Invalidate();
}


void
AxisMeterView::SetValues(const std::vector<float>& values)
{
	fValues = values;
	Invalidate();
}


void
AxisMeterView::Draw()
{
	const float nameWidth = 100.0f;
	const float valueWidth = 44.0f;
	const float barLeft = nameWidth + 8.0f;
	const float barRight = Bounds().right - valueWidth - 8.0f;
	const float center = (barLeft + barRight) / 2.0f;

	BFont font = be_plain_font;
	SetFont(&font);

	for (int32 i = 0; i < (int32)fNames.size(); i++) {
		float top = i * kRowHeight;
		float mid = top + kRowHeight / 2.0f;

		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		BString name(fNames[i]);
		font.TruncateString(&name, B_TRUNCATE_END, nameWidth - 4);
		DrawString(name.String(), BPoint(2, mid + font.Size() / 3));

		// value track with a center rest mark
		SetHighColor(210, 210, 210);
		FillRect(BRect(barLeft, mid - 5, barRight, mid + 5));
		SetHighColor(140, 140, 140);
		StrokeLine(BPoint(center, mid - 6), BPoint(center, mid + 6));

		float value = i < (int32)fValues.size() ? fValues[i] : 0.0f;
		value = std::min(std::max(value, -1.0f), 1.0f);
		if (value != 0.0f) {
			SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			FillRect(BRect(center, mid - 4,
				center + value * (barRight - barLeft) / 2.0f, mid + 4));
		}

		char text[16];
		snprintf(text, sizeof(text), "%+.2f", value);
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(text, BPoint(barRight + 4, mid + font.Size() / 3));
	}
}


// ---- ButtonGridView: one pill per button, lit while pressed ----


class ButtonGridView : public BView {
public:
								ButtonGridView();

			void				SetButtons(const std::vector<BString>& names);
			void				SetStates(const std::vector<char>& states);

	virtual	void				Draw();
	virtual	void				FrameResized(float newWidth, float newHeight);

private:
			void				_RecomputeSize();

			std::vector<BString>	fNames;
			std::vector<char>		fStates;
};


ButtonGridView::ButtonGridView()
	:
	BView("button grid", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
ButtonGridView::SetButtons(const std::vector<BString>& names)
{
	fNames = names;
	fStates.assign(names.size(), 0);

	_RecomputeSize();
	Invalidate();
}


void
ButtonGridView::SetStates(const std::vector<char>& states)
{
	fStates = states;
	Invalidate();
}


void
ButtonGridView::FrameResized(float /*newWidth*/, float /*newHeight*/)
{
	_RecomputeSize();
	if (GetLayout() != NULL)
		GetLayout()->Relayout();
}


void
ButtonGridView::_RecomputeSize()
{
	float width = Bounds().IsValid() ? Bounds().Width() : 320.0f;
	int32 columns = std::max(1.0f, width / (kPillWidth + kPillGap));
	int32 rows = (fNames.size() + columns - 1) / columns;
	float height = std::max((float)rows, 1.0f) * (kPillHeight + kPillGap)
		+ 4.0f;
	SetExplicitMinSize(BSize(280, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNSET, height));
}


void
ButtonGridView::Draw()
{
	BFont font = be_plain_font;
	font.SetSize(10.0f);
	SetFont(&font);

	int32 columns = std::max(1.0f,
		(Bounds().IsValid() ? Bounds().Width() : 320.0f)
			/ (kPillWidth + kPillGap));

	for (int32 i = 0; i < (int32)fNames.size(); i++) {
		float x = (i % columns) * (kPillWidth + kPillGap);
		float y = (i / columns) * (kPillHeight + kPillGap) + 2.0f;
		BRect pill(x, y, x + kPillWidth, y + kPillHeight);
		bool pressed = i < (int32)fStates.size() && fStates[i] != 0;

		if (pressed) {
			SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			FillRect(pill);
			SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		} else {
			SetHighColor(230, 230, 230);
			FillRect(pill);
			SetHighColor(170, 170, 170);
			StrokeRect(pill);
			SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		}

		BString label(fNames[i]);
		font.TruncateString(&label, B_TRUNCATE_MIDDLE, kPillWidth - 8);
		float width = font.StringWidth(label.String());
		DrawString(label.String(),
			BPoint(x + (kPillWidth - width) / 2.0f,
				y + kPillHeight / 2.0f + font.Size() / 3));
	}
}


// ---- TestPanel ----


TestPanel::TestPanel()
	:
	BView("test panel", B_WILL_DRAW | B_NAVIGABLE),
	fRumble(new RumbleTester())
{
	fInfoView = new BStringView("info", B_TRANSLATE("No device selected"));
	fInfoView->SetFont(be_bold_font);

	fAxisView = new AxisMeterView();
	fHatView = new BStringView("hats", "");
	fButtonView = new ButtonGridView();

	fRumbleButton = new BButton(B_TRANSLATE("Test rumble"),
		new BMessage(kMsgRumbleTest));
	fRumbleButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fInfoView)
		.Add(fAxisView)
		.Add(fHatView)
		.Add(fButtonView)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fRumbleButton)
		.End();
}


void
TestPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRumbleTest:
			fRumble->Trigger(500, 0xffff, 0xffff);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
TestPanel::SetDevice(BJoystick* joystick, const char* devicePath)
{
	fRumble->UnsetDevice();

	std::vector<BString> axisNames;
	BString name;
	for (int32 i = 0; joystick != NULL && i < joystick->CountAxes(); i++) {
		joystick->GetAxisNameAt(i, &name);
		axisNames.push_back(name);
	}

	std::vector<BString> buttonNames;
	for (int32 i = 0; joystick != NULL && i < joystick->CountButtons(); i++) {
		joystick->GetButtonNameAt(i, &name);
		buttonNames.push_back(name);
	}

	std::vector<BString> hatNames;
	for (int32 i = 0; joystick != NULL && i < joystick->CountHats(); i++) {
		joystick->GetHatNameAt(i, &name);
		hatNames.push_back(name);
	}

	fAxisView->SetAxes(axisNames);
	fButtonView->SetButtons(buttonNames);
	fAxisValues.assign(axisNames.size(), 0.0f);
	fButtonStates.assign(buttonNames.size(), 0);

	BString hats;
	for (int32 i = 0; i < (int32)hatNames.size(); i++) {
		if (i > 0)
			hats << "   ";
		hats << hatNames[i] << ": " << kHatNames[0];
	}
	fHatView->SetText(hats.String());

	_UpdateInfo(joystick);

	if (devicePath != NULL && devicePath[0] != '\0')
		fRumble->SetDevice(devicePath);
	fRumbleButton->SetEnabled(fRumble->IsSupported());
	fRumbleButton->SetToolTip(fRumble->IsSupported()
		? B_TRANSLATE("Rumble for half a second") : NULL);
}


void
TestPanel::Refresh(BJoystick* joystick, const CalibrationSettings& calibration)
{
	if (joystick == NULL)
		return;

	int32 axisCount = joystick->CountAxes();
	if (axisCount > 0 && (int32)fAxisValues.size() == axisCount) {
		std::vector<int16> raw(axisCount);
		if (joystick->GetAxisValues(&raw[0]) == B_OK) {
			bool changed = false;
			for (int32 i = 0; i < axisCount; i++) {
				float shaped = calibration.Apply(raw[i]);
				if (shaped != fAxisValues[i]) {
					fAxisValues[i] = shaped;
					changed = true;
				}
			}
			if (changed)
				fAxisView->SetValues(fAxisValues);
		}
	}

	int32 buttonCount = joystick->CountButtons();
	if (buttonCount > 0 && (int32)fButtonStates.size() == buttonCount) {
		bool* pressed = new bool[buttonCount];
		if (joystick->GetButtonValues(pressed) == B_OK) {
			bool changed = false;
			for (int32 i = 0; i < buttonCount; i++) {
				char state = pressed[i] ? 1 : 0;
				if (state != fButtonStates[i]) {
					fButtonStates[i] = state;
					changed = true;
				}
			}
			if (changed)
				fButtonView->SetStates(fButtonStates);
		}
		delete[] pressed;
	}

	int32 hatCount = joystick->CountHats();
	if (hatCount > 0) {
		uint8 hats[8];
		if (hatCount <= 8 && joystick->GetHatValues(hats) == B_OK) {
			BString text;
			for (int32 i = 0; i < hatCount; i++) {
				if (i > 0)
					text << "   ";
				uint8 value = hats[i] <= 8 ? hats[i] : 0;
				text << "Hat " << i << ": " << kHatNames[value];
			}
			if (strcmp(fHatView->Text(), text.String()) != 0)
				fHatView->SetText(text.String());
		}
	}
}


void
TestPanel::_UpdateInfo(BJoystick* joystick)
{
	if (joystick == NULL) {
		fInfoView->SetText(B_TRANSLATE("No device selected"));
		return;
	}

	BString controller;
	joystick->GetControllerName(&controller);

	BString info(controller);
	info << "  ·  " << joystick->CountAxes() << " "
		<< B_TRANSLATE("axes") << "  ·  " << joystick->CountButtons()
		<< " " << B_TRANSLATE("buttons") << "  ·  "
		<< joystick->CountHats() << " " << B_TRANSLATE("hats");
	fInfoView->SetText(info.String());
}
