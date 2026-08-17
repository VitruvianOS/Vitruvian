/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceListView.h"

#include <cstdio>

#include <ControlLook.h>
#include <ObjectList.h>

#include <media2/MediaGraph.h>


DeviceListView::DeviceListView(const char* name, bool isOutput)
	:
	BListView(name, B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_FRAME_EVENTS
		| B_NAVIGABLE),
	fIsOutput(isOutput)
{
	SetSelectionMessage(new BMessage(kMsgDeviceSelected));
}


DeviceListView::~DeviceListView()
{
}


void
DeviceListView::Refresh()
{

	while (CountItems() > 0) {
		BListItem* it = RemoveItem((int32)0);
		delete it;
	}

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL)
		return;


	media_client_id defId = 0;
	if (fIsOutput) g->GetDefaultAudioOutput(&defId);
	else           g->GetDefaultAudioInput(&defId);

	BObjectList<media_client_id, true> clients;
	if (g->GetClients(&clients) != B_OK)
		return;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (g->GetClientInfo(id, &info) != B_OK) continue;
		bool match = false;
		info.FindBool(fIsOutput ? "is.sink" : "is.source", &match);
		if (!match) continue;
		BString name;
		info.FindString("name", &name);
		if (name.Length() == 0) name = "<unnamed>";
		AddItem(new DeviceListItem((uint32)id, name.String(),
			(uint32)defId == (uint32)id));
	}

	Invalidate();
}





DeviceListItem::DeviceListItem(uint32 deviceId, const char* name,
		bool isDefault)
	:
	BStringItem(name),
	fDeviceId(deviceId),
	fIsDefault(isDefault)
{
}


void
DeviceListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	BStringItem::DrawItem(owner, frame, complete);


	if (!fIsDefault)
		return;

	const char* kMark = "★";
	owner->SetHighColor(ui_color(B_SUCCESS_COLOR));
	float markWidth = owner->StringWidth(kMark);
	float textX = frame.right - markWidth - 4.0f;
	font_height fh;
	owner->GetFontHeight(&fh);
	float textHeight = fh.ascent + fh.descent;
	float baseline = frame.top + (frame.Height() + textHeight) / 2.0f - 2.0f;
	owner->DrawString(kMark, BPoint(textX, baseline));
}
