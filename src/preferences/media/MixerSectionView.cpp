/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MixerSectionView.h"

#include <stdio.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ObjectList.h>
#include <ScrollView.h>
#include <StringView.h>

#include <media2/MediaGraph.h>

#include "ChannelStripView.h"
#include "MediaMessages.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Mixer"


MixerSectionView::MixerSectionView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fScroller(NULL),
	fRow(NULL)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BStringView* hint = new BStringView("hint",
		B_TRANSLATE("Output, input and application channel strips. "
			"Each strip also shows the controls the device or "
			"application exposes."));

	fRow = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	fRow->GroupLayout()->SetInsets(B_USE_SMALL_SPACING);

	fScroller = new BScrollView("strips_scroll", fRow, 0, true, false);

	BLayoutBuilder::Group<>(GroupLayout())
		.Add(hint)
		.Add(fScroller, 1.0f);
}


MixerSectionView::~MixerSectionView()
{
	_ClearStrips();
}


void
MixerSectionView::_ClearStrips()
{
	for (size_t i = 0; i < fStrips.size(); i++) {
		fStrips[i]->RemoveSelf();
		delete fStrips[i];
	}
	fStrips.clear();

	BGroupLayout* layout = fRow->GroupLayout();
	while (layout->CountItems() > 0) {
		BLayoutItem* item = layout->ItemAt(0);
		layout->RemoveItem(item);
		if (BView* v = item->View()) {
			v->RemoveSelf();
			delete v;
		} else
			delete item;
	}
}


void
MixerSectionView::Refresh()
{
	_ClearStrips();

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		BStringView* label = new BStringView("offline",
			B_TRANSLATE("Audio backend unavailable"));
		fRow->GroupLayout()->AddView(label);
		return;
	}

	BObjectList<media_client_id, true> clients;
	g->GetClients(&clients);

	for (int32 pass = 0; pass < 2; pass++) {
		const bool wantSink = pass == 0;
		for (int32 i = 0; i < clients.CountItems(); i++) {
			media_client_id id = *clients.ItemAt(i);
			BMessage info;
			if (g->GetClientInfo(id, &info) != B_OK)
				continue;
			bool isSink = false;
			bool isSource = false;
			info.FindBool("is.sink", &isSink);
			info.FindBool("is.source", &isSource);
			if (wantSink ? !isSink : !isSource)
				continue;

			BString name;
			info.FindString("name", &name);
			BString nodeName;
			info.FindString("node_name", &nodeName);

			ChannelStripView* strip = new ChannelStripView((uint32)id,
				true, isSink, name.String(), false, nodeName.String());
			fRow->GroupLayout()->AddView(strip);
			fStrips.push_back(strip);
		}
	}

	BObjectList<BMediaGraph::StreamInfo, true> streams;
	if (g->GetStreams(&streams) == B_OK) {
		for (int32 i = 0; i < streams.CountItems(); i++) {
			BMediaGraph::StreamInfo* s = streams.ItemAt(i);
			BString label = s->name;
			if (s->isOutput)
				label << " (" << B_TRANSLATE("playback") << ")";
			else
				label << " (" << B_TRANSLATE("capture") << ")";
			ChannelStripView* strip = new ChannelStripView((uint32)s->id,
				false, s->isOutput, label.String(), true,
				s->nodeName.String());
			fRow->GroupLayout()->AddView(strip);
			fStrips.push_back(strip);
		}
	}

	if (fStrips.empty()) {
		BStringView* label = new BStringView("empty",
			B_TRANSLATE("No audio devices or streams found."));
		fRow->GroupLayout()->AddView(label);
	} else
		BLayoutBuilder::Group<>(fRow->GroupLayout()).AddGlue().End();

	fRow->GroupLayout()->InvalidateLayout();
	InvalidateLayout();
}


void
MixerSectionView::HintAll()
{
	for (size_t i = 0; i < fStrips.size(); i++)
		fStrips[i]->ExternalChangeHint();
}


void
MixerSectionView::MessageReceived(BMessage* msg)
{
	BGroupView::MessageReceived(msg);
}
