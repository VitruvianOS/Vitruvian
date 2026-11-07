/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ChannelStripView.h"

#include <cmath>
#include <stdio.h>

#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringView.h>

#include <media2/MediaGraph.h>
#include <media2/ParameterWeb.h>

#include "LevelMeterView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MixerStrip"


BView*
UnwrapThemeView(BView* themed)
{
	BScrollView* scroller = dynamic_cast<BScrollView*>(themed);
	if (scroller == NULL)
		return themed;

	BView* target = scroller->Target();
	scroller->SetTarget(NULL);
	if (target != NULL && target->Parent() != NULL)
		target->RemoveSelf();
	delete scroller;
	return target;
}


ChannelStripView::ChannelStripView(uint32 nodeId, bool isDevice,
	bool isOutput, const char* name, bool showRoute, const char* nodeName)
	:
	BGroupView("ChannelStrip", B_VERTICAL, B_USE_SMALL_SPACING),
	fNodeId(nodeId),
	fIsDevice(isDevice),
	fIsOutput(isOutput),
	fSuppress(false),
	fLastLocalWrite(0),
	fNodeName(nodeName != NULL ? nodeName : ""),
	fNameLabel(NULL),
	fFader(NULL),
	fMuteBox(NULL),
	fBalance(NULL),
	fMeter(NULL),
	fRouteField(NULL),
	fRouteMenu(NULL),
	fThemeView(NULL),
	fGainParam(NULL),
	fMuteParam(NULL),
	fBalanceParam(NULL)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fNameLabel = new BStringView("name", name != NULL ? name : "");
	fNameLabel->SetFont(be_bold_font);
	fNameLabel->SetTruncation(B_TRUNCATE_MIDDLE);

	fFader = new BSlider("fader", B_TRANSLATE("Volume"),
		new BMessage(kMsgFader), 0, 100, B_VERTICAL, B_BLOCK_THUMB,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	fFader->SetModificationMessage(new BMessage(kMsgFader));
	static const rgb_color kFill = (rgb_color){ 116, 224, 0, 255 };
	fFader->UseFillColor(true, &kFill);
	const float meterMinH = ceilf(be_plain_font->Size() * 8.0f);
	fFader->SetExplicitMinSize(BSize(B_SIZE_UNSET, meterMinH));

	fMuteBox = new BCheckBox("mute", B_TRANSLATE("Mute"),
		new BMessage(kMsgMute));

	fBalance = new BSlider("balance", B_TRANSLATE("Balance"),
		new BMessage(kMsgBalance), -100, 100, B_HORIZONTAL, B_BLOCK_THUMB,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	fBalance->SetModificationMessage(new BMessage(kMsgBalance));
	fBalance->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fBalance->SetHashMarkCount(3);

	fMeter = new LevelMeterView("meter", -60.0f, 0.0f, false);
	const float meterLine = ceilf(be_plain_font->Size() * 1.2f);
	fMeter->SetExplicitMinSize(BSize(meterLine * 6.0f, meterLine * 0.7f));
	fMeter->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, meterLine * 0.7f));

	if (showRoute) {
		fRouteMenu = new BPopUpMenu("route_menu");
		fRouteField = new BMenuField("route", B_TRANSLATE("Output to:"),
			fRouteMenu);
	}

	const float stripWidth = be_plain_font->StringWidth(
		B_TRANSLATE("Volume")) + meterLine * 4.0f;
	SetExplicitMinSize(BSize(stripWidth, B_SIZE_UNSET));
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));

	BLayoutBuilder::Group<>(GroupLayout())
		.Add(fNameLabel)
		.Add(fFader)
		.Add(fMuteBox)
		.Add(fBalance)
		.Add(fMeter);
	if (fRouteField != NULL)
		GroupLayout()->AddView(fRouteField);

	_BindWeb();
	_AddThemeView();
}


ChannelStripView::~ChannelStripView()
{
}


void
ChannelStripView::_BindWeb()
{
	if (fControllable.BindNode(fNodeId) != B_OK)
		return;

	fGainParam = _ParamWithKind(B_MASTER_GAIN);
	if (fGainParam == NULL)
		fGainParam = _ParamWithKind(B_GAIN);
	fMuteParam = _ParamWithKind(B_MUTE);
	fBalanceParam = _ParamWithKind(B_BALANCE);
}


BParameter*
ChannelStripView::_ParamWithKind(const char* kind) const
{
	BParameterWeb* web = fControllable.Web();
	if (web == NULL || kind == NULL)
		return NULL;

	for (int32 g = 0; g < web->CountGroups(); g++) {
		BParameterGroup* group = web->GroupAt(g);
		for (int32 p = 0; p < group->CountParameters(); p++) {
			BParameter* parameter = group->ParameterAt(p);
			if (parameter == NULL)
				continue;
			if (parameter->Kind() != NULL
					&& strcmp(parameter->Kind(), kind) == 0)
				return parameter;
		}
	}
	return NULL;
}


void
ChannelStripView::_AddThemeView()
{
	// Strip-owned controls are flagged hidden in the web; whatever the
	// theme draws here is the node's own extra parameters.
	if (fControllable.Web() == NULL)
		return;

	BView* view = UnwrapThemeView(fControllable.MakeView());
	if (view == NULL)
		return;

	fThemeView = view;
	GroupLayout()->AddView(fThemeView);
}


void
ChannelStripView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fFader->SetTarget(this);
	fMuteBox->SetTarget(this);
	fBalance->SetTarget(this);
	if (fRouteMenu != NULL)
		fRouteMenu->SetTargetForItems(this);

	fControllable.StartControlPanel(BMessenger(this));
	_RefreshFromGraph();
	_RefreshFromWeb();

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;

	status_t metering;
	if (fIsDevice) {
		metering = g->StartMeteringDevice((media_client_id)fNodeId,
			BMessenger(this));
	} else {
		metering = g->StartMeteringNode((media_client_id)fNodeId, !fIsOutput,
			fNodeName.String(), BMessenger(this));
	}
	if (metering != B_OK && !fMeter->IsHidden(fMeter))
		fMeter->Hide();

	if (fRouteMenu != NULL)
		_PopulateRouteMenu();
}


void
ChannelStripView::DetachedFromWindow()
{
	fControllable.StopControlPanel(BMessenger(this));

	BMediaGraph* g = BMediaGraph::Instance();
	if (g != NULL)
		g->StopMeteringNode((media_client_id)fNodeId);
	BGroupView::DetachedFromWindow();
}


void
ChannelStripView::ExternalChangeHint()
{
	static const bigtime_t kLocalWriteSuppression = 1000000;
	if (fLastLocalWrite != 0
			&& system_time() - fLastLocalWrite < kLocalWriteSuppression)
		return;
	_RefreshFromGraph();
	_RefreshFromWeb();
}


void
ChannelStripView::_RefreshFromGraph()
{
	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;

	float master = 0.0f;
	bool mute = false;
	status_t status = B_ERROR;
	if (fIsDevice)
		status = g->GetDeviceVolume((media_client_id)fNodeId, &master, &mute);
	else {
		BObjectList<BMediaGraph::StreamInfo, true> streams;
		if (g->GetStreams(&streams) == B_OK) {
			for (int32 i = 0; i < streams.CountItems(); i++) {
				BMediaGraph::StreamInfo* s = streams.ItemAt(i);
				if ((uint32)s->id == fNodeId) {
					master = s->volume;
					mute = s->mute;
					status = B_OK;
					break;
				}
			}
		}
	}
	if (status != B_OK)
		return;

	if (master < 0.0f) master = 0.0f;
	if (master > 1.0f) master = 1.0f;
	fSuppress = true;
	fFader->SetValue((int32)(master * 100.0f + 0.5f));
	fMuteBox->SetValue(mute ? B_CONTROL_ON : B_CONTROL_OFF);
	fSuppress = false;
}


void
ChannelStripView::_RefreshFromWeb()
{
	if (fGainParam != NULL
			&& fGainParam->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
		BContinuousParameter* cp = (BContinuousParameter*)fGainParam;
		float value = 0.0f;
		size_t size = sizeof(value);
		bigtime_t when = 0;
		if (cp->GetValue(&value, &size, &when) == B_OK && size >= sizeof(value)
				&& cp->MaxValue() > cp->MinValue()) {
			float frac = (value - cp->MinValue())
				/ (cp->MaxValue() - cp->MinValue());
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			fSuppress = true;
			fFader->SetValue((int32)(frac * 100.0f + 0.5f));
			fSuppress = false;
		}
	}

	if (fMuteParam != NULL
			&& fMuteParam->Type() == BParameter::B_DISCRETE_PARAMETER) {
		int32 value = 0;
		size_t size = sizeof(value);
		bigtime_t when = 0;
		if (((BDiscreteParameter*)fMuteParam)->GetValue(&value, &size, &when)
				== B_OK) {
			fSuppress = true;
			fMuteBox->SetValue(value != 0 ? B_CONTROL_ON : B_CONTROL_OFF);
			fSuppress = false;
		}
	}

	if (fThemeView != NULL)
		fThemeView->Invalidate();
	Invalidate();
}


void
ChannelStripView::_ApplyFader()
{
	fLastLocalWrite = system_time();
	float frac = fFader->Value() / 100.0f;
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;

	if (fGainParam != NULL
			&& fGainParam->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
		BContinuousParameter* cp = (BContinuousParameter*)fGainParam;
		const float span = cp->MaxValue() - cp->MinValue();
		float value = cp->MinValue() + frac * span;
		if (value > cp->MaxValue()) value = cp->MaxValue();
		fControllable.SetParameterValue(fGainParam->ID(), system_time(),
			&value, sizeof(value));
		return;
	}

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;
	if (fIsDevice) {
		g->SetDeviceVolume((media_client_id)fNodeId, frac);
	} else {
		g->SetStreamVolume((media_client_id)fNodeId, frac,
			fMuteBox->Value() == B_CONTROL_ON);
	}
}


void
ChannelStripView::_ApplyMute()
{
	fLastLocalWrite = system_time();
	const bool mute = fMuteBox->Value() == B_CONTROL_ON;

	if (fMuteParam != NULL
			&& fMuteParam->Type() == BParameter::B_DISCRETE_PARAMETER) {
		int32 value = mute ? 1 : 0;
		fControllable.SetParameterValue(fMuteParam->ID(), system_time(),
			&value, sizeof(value));
		return;
	}

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;
	if (fIsDevice) {
		g->SetDeviceMute((media_client_id)fNodeId, mute);
	} else {
		float master = fFader->Value() / 100.0f;
		g->SetStreamVolume((media_client_id)fNodeId, master, mute);
	}
}


void
ChannelStripView::_ApplyBalance()
{
	fLastLocalWrite = system_time();

	if (fBalanceParam != NULL) {
		float value = fBalance->Value() / 100.0f;
		fControllable.SetParameterValue(fBalanceParam->ID(), system_time(),
			&value, sizeof(value));
		return;
	}

	// No balance property: fold the slider into the channel volumes,
	// the same trick the legacy mixer used.
	const float master = fFader->Value() / 100.0f;
	const float balance = fBalance->Value() / 100.0f;
	const float left = master * (balance > 0.0f ? 1.0f - balance : 1.0f);
	const float right = master * (balance < 0.0f ? 1.0f + balance : 1.0f);
	const float channels[2] = { left, right };

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;
	if (fIsDevice) {
		g->SetDeviceChannelVolumes((media_client_id)fNodeId, channels, 2);
	} else {
		g->SetStreamChannelVolumes((media_client_id)fNodeId, channels, 2);
	}
}


void
ChannelStripView::_PopulateRouteMenu()
{
	if (fRouteMenu == NULL)
		return;

	for (int32 i = fRouteMenu->CountItems() - 1; i >= 0; i--)
		delete fRouteMenu->RemoveItem(i);

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;

	uint32 currentDeviceId = 0;
	{
		BObjectList<BMediaGraph::StreamInfo, true> streams;
		if (g->GetStreams(&streams) == B_OK) {
			for (int32 i = 0; i < streams.CountItems(); i++) {
				BMediaGraph::StreamInfo* s = streams.ItemAt(i);
				if ((uint32)s->id == fNodeId) {
					currentDeviceId = (uint32)s->deviceId;
					break;
				}
			}
		}
	}

	BObjectList<media_client_id, true> clients;
	if (g->GetClients(&clients) != B_OK)
		return;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (g->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSink = false;
		bool isSource = false;
		info.FindBool("is.sink", &isSink);
		info.FindBool("is.source", &isSource);
		if (fIsOutput ? !isSink : !isSource)
			continue;
		BString name;
		info.FindString("name", &name);
		BMessage* msg = new BMessage(kMsgRoute);
		msg->AddUInt32("device_id", (uint32)id);
		BMenuItem* item = new BMenuItem(name.String(), msg);
		item->SetMarked((uint32)id == currentDeviceId);
		fRouteMenu->AddItem(item);
	}
	fRouteMenu->SetTargetForItems(this);
}


void
ChannelStripView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgPeak:
		{
			uint32 nodeId = 0;
			if (msg->FindUInt32("node_id", &nodeId) == B_OK
					&& nodeId != fNodeId)
				break;
			float peak = 0.0f;
			msg->FindFloat("peak", &peak);
			fMeter->SetPeak(peak);
			break;
		}

		case kMsgFader:
			if (!fSuppress)
				_ApplyFader();
			break;

		case kMsgMute:
			if (!fSuppress)
				_ApplyMute();
			break;

		case kMsgBalance:
			if (!fSuppress)
				_ApplyBalance();
			break;

		case kMsgRoute:
		{
			uint32 deviceId = 0;
			if (fIsDevice || msg->FindUInt32("device_id", &deviceId) != B_OK)
				break;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->MoveStream((media_client_id)fNodeId,
					(media_client_id)deviceId);
			_PopulateRouteMenu();
			break;
		}

		case B_MEDIA_NEW_PARAMETER_VALUE:
			_RefreshFromWeb();
			break;

		default:
			BGroupView::MessageReceived(msg);
			break;
	}
}
