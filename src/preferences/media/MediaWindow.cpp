/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "MediaWindow.h"

#include <algorithm>

#include <Application.h>
#include <Catalog.h>
#include <CardLayout.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <Layout.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuField.h>
#include <Message.h>
#include <MessageRunner.h>
#include <ObjectList.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TabView.h>

#include <media2/MediaGraph.h>

#include <MediaGraphPrivate.h>

#include "DeviceListView.h"
#include "DeviceMixerView.h"
#include "HardwareSectionView.h"
#include "MediaMessages.h"
#include "MixerSectionView.h"
#include "SoundsSectionView.h"
#include "StreamListView.h"
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaWindow"

MediaWindow::MediaWindow(BRect frame, int32 initialSection)
	:
	BWindow(frame,
		B_TRANSLATE_SYSTEM_NAME("Media"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
		| B_NOT_ZOOMABLE),
	fSidebar(NULL),
	fContentPane(NULL),
	fCards(NULL),
	fCurrentSection(NULL),
	fShownSection(kOutputSection),
	fHasShownSection(false),
	fMixerView(NULL),
	fOutputView(NULL),
	fInputView(NULL),
	fStreamsView(NULL),
	fHardwareView(NULL),
	fSoundsView(NULL),
	fStatusLabel(NULL),
	fWatchingPipeWire(false),
	fWatchRetryRunner(NULL)
{
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	_BuildSidebar();

	fContentPane = new BView("content", B_WILL_DRAW | B_FRAME_EVENTS);
	fContentPane->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fCards = new BCardLayout();
	fContentPane->SetLayout(fCards);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_VERTICAL, 0)
			.Add(fSidebar)
			.Add(fStatusLabel)
		.End()
		.AddStrut(B_USE_DEFAULT_SPACING)
		.Add(fContentPane);

	int32 initialRow = _RowForSection(initialSection);
	if (initialRow < 0) {

		initialSection = kMixerSection;
		initialRow = _RowForSection(kMixerSection);
	}
	fSidebar->Select(initialRow);
	_ShowSection((Section)initialSection);

	_TryStartWatching();
	if (!fWatchingPipeWire) {
		fStatusLabel->SetText(B_TRANSLATE("Audio backend unavailable"));
		fWatchRetryRunner = new BMessageRunner(BMessenger(this),
			new BMessage(kMsgRetryWatch), 2000000);
	}

	if (frame == BRect(100, 100, 880, 580))
		CenterOnScreen();
}


MediaWindow::~MediaWindow()
{
	delete fWatchRetryRunner;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kMediaSettingsFile);
		BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
		if (file.InitCheck() == B_OK) {
			BMessage settings;
			settings.AddRect("frame", Frame());
			settings.AddInt32("section", fShownSection);
			settings.Flatten(&file);
		}
	}

	if (fWatchingPipeWire) {
		BMediaGraph* graph = BMediaGraph::Instance();
		if (graph != NULL)
			graph->StopWatching(BMessenger(this));
	}
}


void
MediaWindow::_BuildSidebar()
{
	fSidebar = new BListView("sidebar", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
	fSidebar->SetSelectionMessage(new BMessage(kMsgSectionChanged));
	fSidebarSections.clear();
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Mixer")));
	fSidebarSections.push_back(kMixerSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Output")));
	fSidebarSections.push_back(kOutputSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Input")));
	fSidebarSections.push_back(kInputSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Application volumes")));
	fSidebarSections.push_back(kStreamsSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Hardware")));
	fSidebarSections.push_back(kHardwareSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Sounds")));
	fSidebarSections.push_back(kSoundsSection);

	const float pad = 3 * be_control_look->DefaultItemSpacing();
	float labelWidth = 0;
	for (int32 i = 0; i < fSidebar->CountItems(); i++) {
		BStringItem* item = dynamic_cast<BStringItem*>(fSidebar->ItemAt(i));
		if (item == NULL)
			continue;
		const char* text = item->Text();
		if (text == NULL)
			continue;
		labelWidth = std::max(labelWidth, be_plain_font->StringWidth(text));
		labelWidth = std::max(labelWidth, be_bold_font->StringWidth(text));
	}
	const float sidebarMin = labelWidth + pad;
	fSidebar->SetExplicitMinSize(BSize(sidebarMin, B_SIZE_UNSET));
	fSidebar->SetExplicitMaxSize(BSize(sidebarMin * 1.25f, B_SIZE_UNLIMITED));
	fSidebar->Select(0);

	fStatusLabel = new BStringView("status", "");
}


int32
MediaWindow::_RowForSection(int32 section) const
{
	for (size_t i = 0; i < fSidebarSections.size(); i++) {
		if (fSidebarSections[i] == section)
			return (int32)i;
	}
	return -1;
}

void
MediaWindow::_ShowSection(Section s)
{
	BView* section = NULL;

	switch (s) {
		case kOutputSection:
		case kInputSection:
		{
			const bool wantOutput = (s == kOutputSection);
			if (wantOutput && fOutputView == NULL)
				fOutputView = new DeviceListView("out", true);
			if (!wantOutput && fInputView == NULL)
				fInputView = new DeviceListView("in", false);
			DeviceListView* lv = wantOutput ? fOutputView : fInputView;

			// Built once per list: list, scroller, wrap.
			BView* wrap = lv->Parent() != NULL ? lv->Parent()->Parent() : NULL;

			if (wrap == NULL) {
				BScrollView* scroller = new BScrollView("scroller", lv,
					0, false, true);
				const float scale = be_plain_font->Size() / 12.0f;
				scroller->SetExplicitMinSize(BSize(200 * scale, B_SIZE_UNSET));
				scroller->SetExplicitMaxSize(BSize(250 * scale, B_SIZE_UNSET));

				BGroupView* detail = new BGroupView("detail", B_VERTICAL);
				detail->SetExplicitMinSize(BSize(350 * scale, B_SIZE_UNSET));
				detail->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
					B_ALIGN_USE_FULL_HEIGHT));
				BLayoutBuilder::Group<>(detail)
					.AddGlue()
					.Add(new BStringView("hint",
						wantOutput
						 ? B_TRANSLATE("Select an output device to configure.")
						 : B_TRANSLATE("Select an input device to configure.")))
					.AddGlue();

				BGroupView* group = new BGroupView(B_HORIZONTAL);
				BLayoutBuilder::Group<>(group, B_HORIZONTAL)
					.Add(scroller, 0.3f)
					.AddStrut(B_USE_DEFAULT_SPACING)
					.Add(detail, 0.7f);
				wrap = group;
			}

			section = wrap;
			lv->Refresh();
			lv->SetTargetForMessages(this);
			break;
		}

		case kMixerSection:
			if (fMixerView == NULL)
				fMixerView = new MixerSectionView();
			fMixerView->Refresh();
			section = fMixerView;
			break;

		case kStreamsSection:
			if (fStreamsView == NULL)
				fStreamsView = new StreamListView();
			fStreamsView->Refresh();
			section = fStreamsView;
			break;

		case kHardwareSection:
			if (fHardwareView == NULL)
				fHardwareView = new HardwareSectionView();
			else
				fHardwareView->RefreshDevices();
			section = fHardwareView;
			break;

		case kSoundsSection:
			if (fSoundsView == NULL)
				fSoundsView = new SoundsSectionView();
			section = fSoundsView;
			break;
	}

	if (section == NULL)
		return;

	// Sections stay as cards: the pane keeps the largest one's minimum.
	if (section->Parent() == NULL)
		fCards->AddView(section);
	fCards->SetVisibleItem(fCards->IndexOfView(section));

	fCurrentSection = section;
	fShownSection = s;
	fHasShownSection = true;
}


void
MediaWindow::_OnPipeWireChanged(uint32 what, uint32 deviceId)
{
	if (fOutputView)  fOutputView->Refresh();
	if (fInputView)   fInputView->Refresh();
	if (fStreamsView) fStreamsView->Refresh();

	if (fMixerView != NULL) {
		if (what == BMediaGraph::B_MEDIA_CLIENT_ADDED
				|| what == BMediaGraph::B_MEDIA_CLIENT_REMOVED
				|| what == BMediaGraph::B_MEDIA_STREAMS_CHANGED)
			fMixerView->Refresh();
		else if (what == BMediaGraph::B_MEDIA_VOLUME_CHANGED
				|| what == BMediaGraph::B_MEDIA_DEFAULTS_CHANGED
				|| what == kMsgDevicePortChanged)
			fMixerView->HintAll();
	}

	if (fCurrentSection != NULL) {
		DeviceMixerView* mixer = dynamic_cast<DeviceMixerView*>(
			fCurrentSection->FindView("DeviceMixer"));
		if (mixer != NULL && (what == BMediaGraph::B_MEDIA_VOLUME_CHANGED
				|| what == BMediaGraph::B_MEDIA_DEFAULTS_CHANGED
				|| what == kMsgDevicePortChanged)) {
			if (what == BMediaGraph::B_MEDIA_DEFAULTS_CHANGED
					|| deviceId == 0 || mixer->DeviceId() == deviceId)
				mixer->ExternalChangeHint();
		}
	}

	if (fHardwareView != NULL)
		fHardwareView->NotifyChanged(what, deviceId);
}


void
MediaWindow::_TryStartWatching()
{
	if (fWatchingPipeWire)
		return;

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	if (graph->StartWatching(BMessenger(this)) == B_OK) {
		fWatchingPipeWire = true;
		delete fWatchRetryRunner;
		fWatchRetryRunner = NULL;
		fStatusLabel->SetText("");
	}
}


void
MediaWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgSectionChanged:
		{
			int32 sel = fSidebar->CurrentSelection();
			if (sel < 0 || sel >= (int32)fSidebarSections.size())
				break;
			_ShowSection((Section)fSidebarSections[sel]);
			break;
		}

		case kMsgSelectSection:
		{
			int32 section = kOutputSection;
			msg->FindInt32("section", &section);
			int32 row = _RowForSection(section);
			if (row < 0) {

				section = kOutputSection;
				row = _RowForSection(kOutputSection);
			}

			fSidebar->Select(row);
			if (IsMinimized())
				Minimize(false);
			Activate();
			break;
		}
		case kMsgRefresh:
			if (fOutputView) fOutputView->Refresh();
			if (fInputView)  fInputView->Refresh();
			break;

		case kMsgRetryWatch:
			_TryStartWatching();
			if (fWatchingPipeWire) {
				if (fMixerView != NULL) fMixerView->Refresh();
				if (fOutputView) fOutputView->Refresh();
				if (fInputView)  fInputView->Refresh();
				if (fStreamsView) fStreamsView->Refresh();
			}
			break;

		case kMsgDeviceSelected:
		{

			DeviceListView* lv = NULL;
			int32 sel = fSidebar->CurrentSelection();
			if (sel < 0 || sel >= (int32)fSidebarSections.size())
				break;
			Section s = (Section)fSidebarSections[sel];
			if (s == kOutputSection) lv = fOutputView;
			else if (s == kInputSection) lv = fInputView;
			if (lv == NULL) break;
			int32 idx = lv->CurrentSelection();
			if (idx < 0) break;
			DeviceListItem* item = dynamic_cast<DeviceListItem*>(
				lv->ItemAt(idx));
			if (item == NULL) break;

		if (fCurrentSection == NULL)
			break;
		BGroupView* detail = dynamic_cast<BGroupView*>(
			fCurrentSection->FindView("detail"));
		if (detail == NULL)
			break;

		DeviceMixerView* live = dynamic_cast<DeviceMixerView*>(
			detail->FindView("DeviceMixer"));
		if (live != NULL && live->DeviceId() == item->DeviceId())
			break;
		BGroupLayout* layout = detail->GroupLayout();
		while (layout->CountItems() > 0) {
			BLayoutItem* li = layout->ItemAt(0);
			layout->RemoveItem(li);
			if (BView* v = li->View()) {
				v->RemoveSelf();
				delete v;
			} else
				delete li;
		}
		layout->AddView(new DeviceMixerView(item->DeviceId(), lv->IsOutput()));
		detail->InvalidateLayout();
		break;
		}

		case BMediaGraph::B_MEDIA_CLIENT_ADDED:
		case BMediaGraph::B_MEDIA_CLIENT_REMOVED:
		case BMediaGraph::B_MEDIA_STREAMS_CHANGED:
		case BMediaGraph::B_MEDIA_DEFAULTS_CHANGED:
		case BMediaGraph::B_MEDIA_VOLUME_CHANGED:
		case kMsgDevicePortChanged:
		case kMsgDeviceProfileChanged:
			_OnPipeWireChanged(msg->what,
				msg->GetUInt32("device_id", 0));
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


bool
MediaWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
