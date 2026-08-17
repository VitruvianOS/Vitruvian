/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "VolumePopupView.h"
#include "AudioMixerView.h"

#include <Application.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <Slider.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "VolumePopup"


static const uint32 kMsgMuteToggled = 'VPMt';
static const uint32 kMsgSliderMod   = 'VPSm';
static const uint32 kMsgSliderFinal = 'VPSf';
static const uint32 kMsgAutoClose   = 'VPAc';


static const rgb_color kVolumeFillColor = (rgb_color){ 116, 224, 0, 255 };


VolumePopupView::VolumePopupView(AudioMixerView* target, BPoint anchor,
		float initialVolume, bool initialMute, const BString& deviceName)
	:
	BWindow(BRect(0, 0, 280, 110), "VolumePopup",
		B_BORDERED_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_WILL_ACCEPT_FIRST_CLICK
		| B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_RESIZABLE | B_NOT_ZOOMABLE, 0),
	fTarget(target),

	fSlider(NULL),
	fMuteBox(NULL),
	fDeviceName(deviceName),
	fDragging(false),
	fUpdatedCount(0),
	fClosePending(false)
{
	SetLayout(new BGroupLayout(B_VERTICAL));

	int32 iv = (int32)(initialVolume * 100.0f + 0.5f);

	fSlider = new BSlider("volume", B_TRANSLATE("Volume"),
		new BMessage(kMsgSliderFinal), 0, 100, B_HORIZONTAL,
		B_BLOCK_THUMB, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS);
	fSlider->SetModificationMessage(new BMessage(kMsgSliderMod));
	fSlider->SetLimitLabels(NULL, NULL);

	fSlider->UseFillColor(true, &kVolumeFillColor);
	fSlider->SetBarThickness(7);
	fSlider->SetValue(iv);

	BString label = deviceName;
	if (label.Length() == 0)
		label = B_TRANSLATE("Output");
	BStringView* nameLabel = new BStringView("name", label.String());
	nameLabel->SetFont(be_bold_font);

	fMuteBox = new BCheckBox("mute", B_TRANSLATE("Mute"),
		new BMessage(kMsgMuteToggled));
	fMuteBox->SetValue(initialMute ? B_CONTROL_ON : B_CONTROL_OFF);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(nameLabel)
		.Add(fSlider)
		.Add(fMuteBox);


	ResizeToPreferred();
	float w = Bounds().Width();
	float h = Bounds().Height();
	const float kMargin = 4;
	BRect screen(BScreen(B_MAIN_SCREEN_ID).Frame());

	float left = anchor.x - w / 2.0f;
	float top  = anchor.y - h - kMargin;

	if (top < screen.top + kMargin) top = anchor.y + kMargin;
	if (left < screen.left + kMargin) left = screen.left + kMargin;
	if (left + w > screen.right - kMargin) left = screen.right - kMargin - w;
	if (top + h > screen.bottom - kMargin) top = screen.bottom - kMargin - h;

	MoveTo(left, top);
}


VolumePopupView::~VolumePopupView()
{
	if (fTarget.IsValid())
		fTarget.SendMessage(kMsgRefresh);
}


void
VolumePopupView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSliderMod:
			fDragging = true;
			fUpdatedCount++;
		{
			float v = fSlider->Value() / 100.0f;
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			BMessage fwd(kMsgVolumeChanged);
			fwd.AddFloat("volume", v);
			if (fTarget.IsValid())
				fTarget.SendMessage(&fwd);
		}
		break;

		case kMsgSliderFinal:
		{
			fDragging = false;
			float v = fSlider->Value() / 100.0f;
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			BMessage fwd(kMsgVolumeChanged);
			fwd.AddFloat("volume", v);
			if (fTarget.IsValid())
				fTarget.SendMessage(&fwd);
			if (fUpdatedCount < 2) {


				BMessage close(kMsgAutoClose);
				BMessageRunner::StartSending(this, &close, 150000LL, 1);
			} else
				_RequestClose();
		}
		break;

		case kMsgAutoClose:
			_RequestClose();
			break;

		case kMsgMuteToggled:
			if (fTarget.IsValid())
				fTarget.SendMessage(kMsgToggleMute);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
VolumePopupView::_RequestClose()
{
	if (fClosePending)
		return;
	fClosePending = true;
	PostMessage(B_QUIT_REQUESTED);
}


bool
VolumePopupView::QuitRequested()
{

	if (fTarget.IsValid())
		fTarget.SendMessage('AMPG');
	return true;
}


void
VolumePopupView::WindowActivated(bool active)
{
	if (active)
		return;

	if (fDragging)
		return;
	_RequestClose();
}
