/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceMixerView.h"

#include "MediaMessages.h"

#include <algorithm>

#include <OS.h>

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>

#include <media2/MediaGraph.h>

#include "LevelMeterView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeviceMixer"

DeviceMixerView::DeviceMixerView(uint32 deviceId, bool isOutput)
	:

	BGroupView("DeviceMixer", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fDeviceId(deviceId),
	fIsOutput(isOutput),
	fIsActive(false),
	fLastLocalWrite(0),
	fNameLabel(NULL),
	fMasterSlider(NULL),
	fMuteBox(NULL),
	fBalanceSlider(NULL),
	fDefaultButton(NULL),
	fPortField(NULL),
	fPortMenu(NULL),
	fMeter(NULL),
	fFormatLabel(NULL)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fNameLabel = new BStringView("name", "");
	fNameLabel->SetFont(be_bold_font);

	fMasterSlider = new BSlider("master",
		B_TRANSLATE("Volume"), new BMessage(kMsgVolume),
		0, 150, B_HORIZONTAL, B_BLOCK_THUMB,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	fMasterSlider->SetModificationMessage(new BMessage(kMsgVolume));

	static const rgb_color kFill = (rgb_color){ 116, 224, 0, 255 };
	fMasterSlider->UseFillColor(true, &kFill);
	fMasterSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fMasterSlider->SetHashMarkCount(6);
	fMasterSlider->SetValue(75);
	fMasterSlider->SetExplicitMinSize(BSize(B_SIZE_UNSET, 30.0f));

	fMuteBox = new BCheckBox("mute", B_TRANSLATE("Mute"),
		new BMessage(kMsgMute));

	fBalanceSlider = new BSlider("balance",
		B_TRANSLATE("Balance"), new BMessage(kMsgBalance),
		-100, 100, B_HORIZONTAL, B_BLOCK_THUMB,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	fBalanceSlider->SetModificationMessage(new BMessage(kMsgBalance));
	fBalanceSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fBalanceSlider->SetHashMarkCount(5);

	fPortMenu = new BPopUpMenu("port_menu");
	fPortField = new BMenuField("port_field", B_TRANSLATE("Port:"), fPortMenu);
	fPortField->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	fFormatLabel = new BStringView("format", "");

	fMeter = new LevelMeterView("meter", -60.0f, 0.0f, true);
	fMeter->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120.0f));
	fMeter->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 120.0f));

	fDefaultButton = new BButton("default",
		fIsOutput ? B_TRANSLATE("Set as Default Output")
		          : B_TRANSLATE("Set as Default Input"),
		new BMessage(kMsgSetDefault));

	fMasterSlider->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_VERTICAL_CENTER));
	fMeter->SetExplicitMinSize(BSize(20.0f, B_SIZE_UNSET));
	fMeter->SetExplicitMaxSize(BSize(60.0f, B_SIZE_UNSET));
	SetExplicitMinSize(BSize(300.0f, B_SIZE_UNSET));
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT));

	GroupLayout()->SetSpacing(B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(GroupLayout())
		.Add(fNameLabel)
		.Add(fMasterSlider)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fMuteBox)
			.AddGlue()
		.End()
		.Add(fBalanceSlider)
		.Add(fPortField)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.AddGlue()
			.Add(fMeter)
			.AddGlue()
		.End()
		.Add(fFormatLabel)
		.Add(fDefaultButton);
}


DeviceMixerView::~DeviceMixerView()
{
}


void
DeviceMixerView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fMasterSlider->SetTarget(this);
	fMuteBox->SetTarget(this);
	fBalanceSlider->SetTarget(this);
	fDefaultButton->SetTarget(this);
	fPortMenu->SetTargetForItems(this);
	RefreshFromBackend();

	BMediaGraph* g = BMediaGraph::Instance();
	if (g != NULL)
		g->StartMeteringDevice((media_client_id)fDeviceId, BMessenger(this));
}


void
DeviceMixerView::DetachedFromWindow()
{
	BMediaGraph* g = BMediaGraph::Instance();
	if (g != NULL)
		g->StopMeteringDevice((media_client_id)fDeviceId);
	BGroupView::DetachedFromWindow();
}


void
DeviceMixerView::Pulse()
{
	if (!fIsActive)
		return;
}


void
DeviceMixerView::SetActive(bool active)
{
	fIsActive = active;
}


void
DeviceMixerView::ExternalChangeHint()
{
	static const bigtime_t kLocalWriteSuppression = 1000000;
	if (fLastLocalWrite != 0
			&& system_time() - fLastLocalWrite < kLocalWriteSuppression)
		return;
	RefreshFromBackend();
}


void
DeviceMixerView::RefreshFromBackend()
{
	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		fNameLabel->SetText(B_TRANSLATE("Audio backend unavailable"));
		return;
	}

	BMessage info;
	status_t infoStatus = g->GetClientInfo((media_client_id)fDeviceId, &info);
	if (infoStatus == B_OK) {
		BString name;
		info.FindString("name", &name);
		if (name.Length() == 0) name = "<unnamed>";
		fNameLabel->SetText(name.String());

		bool isSink = false;
		bool isSource = false;
		info.FindBool("is.sink",   &isSink);
		info.FindBool("is.source", &isSource);

		fFormatLabel->SetText(isSink
			? B_TRANSLATE("Output device")
			: (isSource ? B_TRANSLATE("Input device") : ""));
	} else {
		fNameLabel->SetText("Unknown device");
		fFormatLabel->SetText("");
	}

	float master = 0.75f;
	bool mute = false;
	status_t volStatus = g->GetDeviceVolume((media_client_id)fDeviceId, &master, &mute);
	if (volStatus == B_OK) {
		if (master < 0.0f) master = 0.0f;
		if (master > 1.0f) master = 1.0f;
		int32 v = (int32)(master * 100.0f + 0.5f);
		fMasterSlider->SetValue(v);
		fMuteBox->SetValue(mute ? B_CONTROL_ON : B_CONTROL_OFF);
	} else {
		fMasterSlider->SetValue(75);
		fMuteBox->SetValue(B_CONTROL_OFF);
	}

	media_client_id defId = 0;
	if (fIsOutput)
		g->GetDefaultAudioOutput(&defId);
	else
		g->GetDefaultAudioInput(&defId);
	fDefaultButton->SetEnabled((uint32)defId != fDeviceId);

	_PopulatePorts();

	Invalidate();
}


void
DeviceMixerView::_PopulatePorts()
{
	if (fPortMenu == NULL)
		return;

	for (int32 i = fPortMenu->CountItems() - 1; i >= 0; i--)
		delete fPortMenu->RemoveItem(i);
	fPortIds.clear();

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("Audio backend unavailable"), NULL);
		item->SetEnabled(false);
		fPortMenu->AddItem(item);
		return;
	}

	BObjectList<BMediaGraph::DevicePortInfo, true> ports;
	status_t status = g->GetDevicePorts((media_client_id)fDeviceId, &ports);

	if (status == B_NOT_SUPPORTED || (status == B_OK && ports.CountItems() == 0)) {
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("No ports reported"), NULL);
		item->SetEnabled(false);
		fPortMenu->AddItem(item);
		return;
	}
	if (status != B_OK) {
		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("Failed to load ports"), NULL);
		item->SetEnabled(false);
		fPortMenu->AddItem(item);
		return;
	}

	for (int32 i = 0; i < ports.CountItems(); i++) {
		BMediaGraph::DevicePortInfo* p = ports.ItemAt(i);
		BMenuItem* item = new BMenuItem(p->description.String(),
			new BMessage(kMsgPort));
		item->Message()->AddInt32("port_id", p->portId);
		fPortMenu->AddItem(item);
		fPortIds.push_back(p->portId);
		if (p->active)
			item->SetMarked(true);
	}

	fPortMenu->SetTargetForItems(this);
}


void
DeviceMixerView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgPeakUpdate:
		{
			float peak = 0.0f;
			msg->FindFloat("peak", &peak);
			fMeter->SetPeak(peak);
			break;
		}
		case kMsgVolume:
		{
			fLastLocalWrite = system_time();
			float v = fMasterSlider->Value() / 100.0f;
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->SetDeviceVolume((media_client_id)fDeviceId, v);
			break;
		}
		case kMsgBalance:
		{
			fLastLocalWrite = system_time();

			float master = fMasterSlider->Value() / 100.0f;
			if (master < 0.0f) master = 0.0f;
			if (master > 1.0f) master = 1.0f;
			float balance = fBalanceSlider->Value() / 100.0f;
			float left  = master * (balance > 0.0f ? 1.0f - balance : 1.0f);
			float right = master * (balance < 0.0f ? 1.0f + balance : 1.0f);
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL) {
				float channels[2] = { left, right };
				g->SetDeviceChannelVolumes((media_client_id)fDeviceId,
					channels, 2);
			}
			break;
		}
		case kMsgMute:
		{
			fLastLocalWrite = system_time();
			bool mute = fMuteBox->Value() == B_CONTROL_ON;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->SetDeviceMute((media_client_id)fDeviceId, mute);
			break;
		}
		case kMsgPort:
		{
			int32 portId = -1;
			if (msg->FindInt32("port_id", &portId) != B_OK || portId < 0)
				break;
			fLastLocalWrite = system_time();
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->SetDevicePort((media_client_id)fDeviceId, portId);

			_PopulatePorts();
			break;
		}
		case kMsgSetDefault:
		{
			BMediaGraph* g = BMediaGraph::Instance();
			if (g == NULL) break;
			if (fIsOutput)
				g->SetDefaultAudioOutput((media_client_id)fDeviceId);
			else
				g->SetDefaultAudioInput((media_client_id)fDeviceId);
			Window()->PostMessage(kMsgRefresh);
			break;
		}
		default:
			BGroupView::MessageReceived(msg);
			break;
	}
}
