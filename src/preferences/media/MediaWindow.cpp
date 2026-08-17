/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "MediaWindow.h"

#include <algorithm>
#include <stdio.h>

#include <Application.h>
#include <Catalog.h>
#include <CheckBox.h>
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
#include <ObjectList.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TabView.h>

#include <media2/MediaGraph.h>

#include "DeviceListView.h"
#include "DeviceMixerView.h"
#include "MediaMessages.h"
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
	fCurrentSection(NULL),
	fShownSection(kOutputSection),
	fHasShownSection(false),
	fOutputView(NULL),
	fInputView(NULL),
	fStreamsView(NULL),
	fHardwareView(NULL),
	fSoundsView(NULL),
	fStatusLabel(NULL),
	fWatchingPipeWire(false),

	fHardwareDeviceMenu(NULL),
	fHardwareProfileList(NULL),
	fHardwareStatus(NULL),
	fHardwareSelectedDevice(0),
	fHardwareActiveProfileIndex(-1),
	fHardwareLastLocalWrite(0)
{
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	_BuildSidebar();

	fContentPane = new BView("content", B_WILL_DRAW | B_FRAME_EVENTS);
	fContentPane->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fContentPane->SetLayout(new BGroupLayout(B_VERTICAL));

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

		initialSection = kOutputSection;
		initialRow = _RowForSection(kOutputSection);
	}
	fSidebar->Select(initialRow);
	_ShowSection((Section)initialSection);

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph != NULL) {
		fWatchingPipeWire =
			graph->StartWatching(BMessenger(this)) == B_OK;
	}

	if (graph == NULL)
		fStatusLabel->SetText(B_TRANSLATE("Audio backend unavailable"));
	else
		fStatusLabel->SetText("");

	if (frame == BRect(100, 100, 880, 580))
		CenterOnScreen();
}


MediaWindow::~MediaWindow()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kMediaSettingsFile);
		BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
		if (file.InitCheck() == B_OK) {
			BRect rect = Frame();
			char buffer[128];
			int len = snprintf(buffer, sizeof(buffer),
				"rect = %d,%d,%d,%d\nsection = %d\n",
				(int)rect.left, (int)rect.top, (int)rect.right,
				(int)rect.bottom, (int)fShownSection);
			file.Write(buffer, len);
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
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Output")));
	fSidebarSections.push_back(kOutputSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Input")));
	fSidebarSections.push_back(kInputSection);
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Application volumes")));
	fSidebarSections.push_back(kStreamsSection);
#if MEDIA_PREFS_ENABLE_HARDWARE
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Hardware")));
	fSidebarSections.push_back(kHardwareSection);
#endif
	fSidebar->AddItem(new BStringItem(B_TRANSLATE("Sounds")));
	fSidebarSections.push_back(kSoundsSection);
	fSidebar->SetExplicitMinSize(BSize(180.0f, B_SIZE_UNSET));
	fSidebar->SetExplicitMaxSize(BSize(220.0f, B_SIZE_UNLIMITED));
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
	if (fCurrentSection != NULL && fHasShownSection
			&& fShownSection == kHardwareSection) {
		fCurrentSection->RemoveSelf();
		fCurrentSection = NULL;

	}

	if (fCurrentSection != NULL) {
		if (fHasShownSection) {
			switch (fShownSection) {
				case kOutputSection:
					fOutputView = NULL;
					break;
				case kInputSection:
					fInputView = NULL;
					break;
				case kStreamsSection:
					fStreamsView = NULL;
					break;
				case kSoundsSection:
					fSoundsView = NULL;
					break;
				case kHardwareSection:

					break;
				default:
					break;
			}
		}

		fCurrentSection->RemoveSelf();
		delete fCurrentSection;
		fCurrentSection = NULL;
	}

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

			BScrollView* scroller = new BScrollView("scroller", lv,
				0, false, true);
			scroller->SetExplicitMinSize(BSize(200.0f, B_SIZE_UNSET));
			scroller->SetExplicitMaxSize(BSize(250.0f, B_SIZE_UNSET));

			BGroupView* detail = new BGroupView("detail", B_VERTICAL);
			detail->SetExplicitMinSize(BSize(350.0f, B_SIZE_UNSET));
			detail->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
				B_ALIGN_USE_FULL_HEIGHT));
			BLayoutBuilder::Group<>(detail)
				.AddGlue()
				.Add(new BStringView("hint",
					wantOutput
					 ? B_TRANSLATE("Select an output device to configure.")
					 : B_TRANSLATE("Select an input device to configure.")))
				.AddGlue();

			BGroupView* wrap = new BGroupView(B_HORIZONTAL);
			BLayoutBuilder::Group<>(wrap, B_HORIZONTAL)
				.Add(scroller, 0.3f)
				.AddStrut(B_USE_DEFAULT_SPACING)
				.Add(detail, 0.7f);

			fCurrentSection = wrap;

			lv->Refresh();
			lv->SetTargetForMessages(this);
			break;
		}

		case kStreamsSection:
			if (fStreamsView == NULL)
				fStreamsView = new StreamListView();
			((StreamListView*)fStreamsView)->Refresh();
			fCurrentSection = fStreamsView;
			break;

		case kHardwareSection:
		{
			if (fHardwareView != NULL) {
				fCurrentSection = fHardwareView;
				_PopulateHardwareDeviceMenu();
				break;
			}

			BGroupView* hw = new BGroupView(B_VERTICAL);
			BLayoutBuilder::Group<>(hw)
				.SetInsets(B_USE_SMALL_SPACING)
				.Add(new BStringView("title",
					B_TRANSLATE("Device profiles")))
				.Add(new BStringView("desc",
					B_TRANSLATE("Select a profile configuration for the audio device.")));

			fHardwareDeviceMenu = new BPopUpMenu("device_menu");
			BMenuField* deviceField = new BMenuField("device_field",
				B_TRANSLATE("Device:"), fHardwareDeviceMenu);
			deviceField->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
				B_ALIGN_VERTICAL_CENTER));

			fHardwareProfileList = new BListView("profile_list",
				B_SINGLE_SELECTION_LIST);
			fHardwareProfileList->SetInvocationMessage(
				new BMessage(kMsgHardwareProfileSelected));
			fHardwareProfileList->SetSelectionMessage(
				new BMessage(kMsgHardwareProfileSelected));
			fHardwareProfileList->SetTarget(this);
			BScrollView* profileScroll = new BScrollView("profile_scroll",
				fHardwareProfileList, 0, false, true, B_FANCY_BORDER);
			profileScroll->SetExplicitMinSize(BSize(200.0f, 150.0f));

			fHardwareStatus = new BStringView("hardware_status", "");

			BLayoutBuilder::Group<>(hw)
				.Add(deviceField)
				.Add(profileScroll)
				.Add(fHardwareStatus)
				.AddGlue();

			fHardwareSelectedDevice = 0;
			fHardwareActiveProfileIndex = -1;

			_PopulateHardwareDeviceMenu();
			fHardwareView = hw;
			fCurrentSection = hw;
			break;
		}

		case kSoundsSection:

			if (fSoundsView == NULL)
				fSoundsView = new SoundsSectionView();
			fCurrentSection = fSoundsView;
			break;
	}

	if (fCurrentSection != NULL)
		fContentPane->AddChild(fCurrentSection);

	fShownSection = s;
	fHasShownSection = true;
}


void
MediaWindow::_PopulateHardwareDeviceMenu()
{
	if (fHardwareDeviceMenu == NULL)
		return;

	fHardwareDeviceMenu->RemoveItems(0, fHardwareDeviceMenu->CountItems());

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK)
		return;

	bool added = false;
	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (graph->GetClientInfo(id, &info) != B_OK)
			continue;

		bool isSink = false;
		bool isSource = false;
		if (info.FindBool("is.sink", &isSink) != B_OK)
			isSink = false;
		if (info.FindBool("is.source", &isSource) != B_OK)
			isSource = false;

		if (!isSink && !isSource)
			continue;

		BString name;
		if (info.FindString("name", &name) != B_OK)
			continue;

		BMenuItem* item = new BMenuItem(name.String(),
			new BMessage(kMsgHardwareDeviceSelected));
		item->Message()->AddInt32("device_id", id);

		if (isSink) {
			fHardwareDeviceMenu->AddItem(item, 0);
		} else {
			fHardwareDeviceMenu->AddItem(item);
		}

		added = true;
	}

	if (!added) {
		BMenuItem* noItem = new BMenuItem(
			B_TRANSLATE("No audio devices found"), NULL);
		noItem->SetEnabled(false);
		fHardwareDeviceMenu->AddItem(noItem);
	}
	fHardwareDeviceMenu->SetTargetForItems(this);

	if (!added)
		return;

	BMenuItem* toMark = NULL;
	for (int32 i = 0; i < fHardwareDeviceMenu->CountItems(); i++) {
		BMenuItem* item = fHardwareDeviceMenu->ItemAt(i);
		int32 id = 0;
		if (item->Message() == NULL
				|| item->Message()->FindInt32("device_id", &id) != B_OK)
			continue;
		if ((media_client_id)id == fHardwareSelectedDevice) {
			toMark = item;
			break;
		}
		if (toMark == NULL)
			toMark = item;
	}
	if (toMark == NULL)
		return;

	toMark->SetMarked(true);
	int32 markedId = 0;
	toMark->Message()->FindInt32("device_id", &markedId);
	if ((media_client_id)markedId != fHardwareSelectedDevice) {
		fHardwareSelectedDevice = (media_client_id)markedId;
		_PopulateHardwareProfiles();
	}
}


void
MediaWindow::_PopulateHardwareProfiles()
{
	if (fHardwareProfileList == NULL || fHardwareStatus == NULL)
		return;

	fHardwareProfileList->RemoveItems(0, fHardwareProfileList->CountItems());
	fHardwareStatus->SetText("");

	if (fHardwareSelectedDevice == 0) {
		fHardwareStatus->SetText(B_TRANSLATE("No device selected"));
		return;
	}

	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL) {
		fHardwareStatus->SetText(B_TRANSLATE("Media graph unavailable"));
		return;
	}

	BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
	int32 activeIndex = -1;
	status_t status = graph->GetDeviceProfiles(fHardwareSelectedDevice,
		&profiles, &activeIndex);

	if (status == B_NOT_SUPPORTED) {
		fHardwareStatus->SetText(B_TRANSLATE("No profiles reported"));
		return;
	} else if (status != B_OK) {
		fHardwareStatus->SetText(B_TRANSLATE("Failed to load profiles"));
		return;
	}

	if (profiles.CountItems() == 0) {
		fHardwareStatus->SetText(B_TRANSLATE("No profiles available"));
		return;
	}

	fHardwareActiveProfileIndex = activeIndex;
	fHardwareProfileIndices.clear();

	for (int32 i = 0; i < profiles.CountItems(); i++) {
		BMediaGraph::DeviceProfileInfo* info = profiles.ItemAt(i);
		BString display = info->description;
		if (!info->available)
			display << " (" << B_TRANSLATE("unavailable") << ")";

		BStringItem* item = new BStringItem(display.String());
		fHardwareProfileList->AddItem(item);
		fHardwareProfileIndices.push_back(info->index);

		if (info->index == activeIndex) {
			fHardwareProfileList->Select(i);
			item->SetEnabled(false);
		}
	}
}


void
MediaWindow::_OnPipeWireChanged(uint32 what, uint32 deviceId)
{
	if (fOutputView)  fOutputView->Refresh();
	if (fInputView)   fInputView->Refresh();
	if (fStreamsView) ((StreamListView*)fStreamsView)->Refresh();

	if (what == kMsgPWDeviceVolumeChanged || what == kMsgPWDefaultChanged
			|| what == kMsgPWDevicePortChanged) {
		if (fCurrentSection != NULL) {
			DeviceMixerView* mixer = dynamic_cast<DeviceMixerView*>(
				fCurrentSection->FindView("DeviceMixer"));
			if (mixer != NULL
					&& (what == kMsgPWDefaultChanged
						|| deviceId == 0 || mixer->DeviceId() == deviceId)) {
				mixer->ExternalChangeHint();
			}
		}
	}

#if MEDIA_PREFS_ENABLE_HARDWARE

	if (what == kMsgPWDeviceProfileChanged || what == kMsgPWDevicePortChanged)
		_PopulateHardwareProfiles();

	if (what == kMsgPWDevicesChanged) {
		_PopulateHardwareDeviceMenu();
	}
#endif
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

		case kMsgHardwareDeviceSelected:
		{
			int32 id = 0;
			if (msg->FindInt32("device_id", &id) != B_OK)
				break;
			fHardwareSelectedDevice = (media_client_id)id;
			_PopulateHardwareProfiles();
			break;
		}

		case kMsgHardwareProfileSelected:
		{
			int32 row = fHardwareProfileList != NULL
				? fHardwareProfileList->CurrentSelection() : -1;
			if (row < 0 || row >= (int32)fHardwareProfileIndices.size())
				break;
			int32 profileIndex = fHardwareProfileIndices[row];

			if (profileIndex == fHardwareActiveProfileIndex)
				break;

			BMediaGraph* graph = BMediaGraph::Instance();
			status_t status = graph != NULL
				? graph->SetDeviceProfile(fHardwareSelectedDevice, profileIndex)
				: B_DEVICE_NOT_FOUND;

			_PopulateHardwareProfiles();
			if (status != B_OK && fHardwareStatus != NULL) {
				fHardwareStatus->SetText(
					B_TRANSLATE("Failed to switch profile"));
			}
			break;
		}

		case kMsgDeviceSelected:
		{

			DeviceListView* lv = NULL;
			Section s = (Section)fSidebar->CurrentSelection();
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

		case kMsgPWDevicesChanged:
		case kMsgPWStreamsChanged:
		case kMsgPWDefaultChanged:
		case kMsgPWDeviceVolumeChanged:
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
