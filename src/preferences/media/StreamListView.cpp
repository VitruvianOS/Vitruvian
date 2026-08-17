/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "StreamListView.h"

#include "MediaMessages.h"

#include <CheckBox.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringItem.h>
#include <StringView.h>

#include <media2/MediaGraph.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Streams"


StreamListView::StreamListView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fList(NULL),
	fVolume(NULL),
	fMute(NULL),
	fRouteMenu(NULL),
	fRouteField(NULL),
	fEmptyLabel(NULL),
	fCurrentStreamId(0),
	fSuppressVolumeMsg(false)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fList = new BListView("streams", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
	fList->SetSelectionMessage(new BMessage(kMsgStreamSelected));
	BScrollView* scroller = new BScrollView("scroller", fList,
		0, false, true);

	fEmptyLabel = new BStringView("empty",
		B_TRANSLATE("No applications are playing or recording audio."));

	fVolume = new BSlider("streamvol",
		B_TRANSLATE("Volume"), new BMessage(kMsgStreamVolume),
		0, 150, B_HORIZONTAL, B_BLOCK_THUMB,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS);
	fVolume->SetModificationMessage(new BMessage(kMsgStreamVolume));
	fVolume->SetEnabled(false);

	fMute = new BCheckBox("streammute", B_TRANSLATE("Mute"),
		new BMessage(kMsgStreamMute));
	fMute->SetEnabled(false);

	fRouteMenu = new BPopUpMenu(B_TRANSLATE("Output to"), true, false);
	fRouteField = new BMenuField("route", B_TRANSLATE("Route to:"),
		fRouteMenu);
	fRouteField->SetEnabled(false);



	BLayoutBuilder::Group<>(GroupLayout())
		.Add(scroller)
		.Add(fEmptyLabel)
		.Add(fVolume)
		.AddGroup(B_HORIZONTAL)
			.Add(fMute)
			.AddGlue()
		.End()
		.Add(fRouteField);
}


StreamListView::~StreamListView()
{
}


void
StreamListView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fList->SetTarget(this);
	fVolume->SetTarget(this);
	fMute->SetTarget(this);
	Refresh();
}


void
StreamListView::Refresh()
{

	while (fList->CountItems() > 0) {
		BListItem* it = fList->RemoveItem((int32)0);
		delete it;
	}

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		fEmptyLabel->SetText(B_TRANSLATE("Audio backend unavailable."));
		return;
	}

	BObjectList<BMediaGraph::StreamInfo, true> streams;
	if (g->GetStreams(&streams) != B_OK)
		return;

	for (int32 i = 0; i < streams.CountItems(); i++) {
		BMediaGraph::StreamInfo* s = streams.ItemAt(i);
		BString label = s->name;
		if (s->isOutput)
			label << "  ▶  " << B_TRANSLATE("Playback");
		else
			label << "  ●  " << B_TRANSLATE("Capture");
		fList->AddItem(new BStringItem(label.String()));
	}

	bool empty = fList->CountItems() == 0;
	if (empty && fEmptyLabel->IsHidden())
		fEmptyLabel->Show();
	else if (!empty && !fEmptyLabel->IsHidden())
		fEmptyLabel->Hide();

	_ShowDetailFor(fList->CurrentSelection());
}


void
StreamListView::_ShowDetailFor(int32 index)
{
	fCurrentStreamId = 0;
	fVolume->SetEnabled(false);
	fMute->SetEnabled(false);
	fRouteField->SetEnabled(false);
	if (index < 0)
		return;

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;

	BObjectList<BMediaGraph::StreamInfo, true> streams;
	if (g->GetStreams(&streams) != B_OK)
		return;
	BMediaGraph::StreamInfo* s = streams.ItemAt(index);
	if (s == NULL)
		return;

	fCurrentStreamId = (uint32)s->id;
	fSuppressVolumeMsg = true;
	int32 v = (int32)(s->volume * 100.0f + 0.5f);
	fVolume->SetValue(v);
	fSuppressVolumeMsg = false;
	fVolume->SetEnabled(true);
	fMute->SetValue(s->mute ? B_CONTROL_ON : B_CONTROL_OFF);
	fMute->SetEnabled(true);

	_PopulateRouteMenu(s->isOutput, (uint32)s->deviceId);
	fRouteField->SetEnabled(true);
}


void
StreamListView::_PopulateRouteMenu(bool isOutput, uint32 currentDeviceId)
{
	for (int32 i = fRouteMenu->CountItems() - 1; i >= 0; i--)
		delete fRouteMenu->RemoveItem(i);

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;

	BObjectList<media_client_id, true> clients;
	if (g->GetClients(&clients) != B_OK)
		return;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (g->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSink = false, isSource = false;
		info.FindBool("is.sink", &isSink);
		info.FindBool("is.source", &isSource);
		if (isOutput ? !isSink : !isSource)
			continue;
		BString name;
		info.FindString("name", &name);
		BMessage* msg = new BMessage(kMsgStreamRouted);
		msg->AddUInt32("device_id", (uint32)id);
		BMenuItem* item = new BMenuItem(name.String(), msg);
		item->SetMarked((uint32)id == currentDeviceId);
		fRouteMenu->AddItem(item);
	}
	fRouteMenu->SetTargetForItems(this);
}


void
StreamListView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgStreamSelected:
			_ShowDetailFor(fList->CurrentSelection());
			break;

		case kMsgStreamRouted:
		{
			uint32 deviceId;
			if (fCurrentStreamId == 0
				|| msg->FindUInt32("device_id", &deviceId) != B_OK) {
				break;
			}
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->MoveStream((media_client_id)fCurrentStreamId,
					(media_client_id)deviceId);
			break;
		}

		case kMsgStreamVolume:
		{
			if (fSuppressVolumeMsg || fCurrentStreamId == 0) break;
			float v = fVolume->Value() / 100.0f;
			if (v < 0.0f) v = 0.0f;
			if (v > 1.0f) v = 1.0f;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->SetStreamVolume((media_client_id)fCurrentStreamId,
					v, fMute->Value() == B_CONTROL_ON);
			break;
		}
		case kMsgStreamMute:
		{
			if (fCurrentStreamId == 0) break;
			bool mute = fMute->Value() == B_CONTROL_ON;
			float v = fVolume->Value() / 100.0f;
			BMediaGraph* g = BMediaGraph::Instance();
			if (g != NULL)
				g->SetStreamVolume((media_client_id)fCurrentStreamId,
					v, mute);
			break;
		}
		default:
			BGroupView::MessageReceived(msg);
			break;
	}
}
