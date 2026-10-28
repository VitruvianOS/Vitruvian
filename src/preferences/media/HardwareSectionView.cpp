/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareSectionView.h"

#include <stdio.h>

#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <GroupView.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <Slider.h>
#include <StringItem.h>
#include <StringView.h>

#include <media2/MediaGraph.h>
#include <media2/ParameterWeb.h>

#include <MediaGraphPrivate.h>

#include "ChannelStripView.h"
#include "MediaMessages.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


HardwareSectionView::HardwareSectionView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fDeviceMenu(NULL),
	fProfileList(NULL),
	fStatus(NULL),
	fParamScroll(NULL),
	fParamView(NULL),
	fAlsaGroup(NULL),
	fAlsaScroll(NULL),
	fSelectedDevice(0),
	fActiveProfile(-1)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fDeviceMenu = new BPopUpMenu("device_menu");
	BMenuField* deviceField = new BMenuField("device_field",
		B_TRANSLATE("Device:"), fDeviceMenu);
	deviceField->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	fProfileList = new BListView("profile_list", B_SINGLE_SELECTION_LIST);
	fProfileList->SetInvocationMessage(
		new BMessage(kMsgHardwareProfileSelected));
	fProfileList->SetSelectionMessage(
		new BMessage(kMsgHardwareProfileSelected));
	BScrollView* profileScroll = new BScrollView("profile_scroll",
		fProfileList, 0, false, true, B_FANCY_BORDER);
	const float scale = be_plain_font->Size() / 12.0f;
	profileScroll->SetExplicitMinSize(BSize(200 * scale, 100 * scale));

	fStatus = new BStringView("hardware_status", "");

	fParamView = new BView("device_params", B_WILL_DRAW);
	fParamView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fParamScroll = new BScrollView("device_params_scroll", fParamView,
		0, false, true);
	fParamScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 90 * scale));
	fParamScroll->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));

	fAlsaGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fAlsaGroup->GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	fAlsaScroll = new BScrollView("alsa_scroll", fAlsaGroup, 0, false, true);
	fAlsaScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 90 * scale));

	BLayoutBuilder::Group<>(GroupLayout())
		.Add(new BStringView("title", B_TRANSLATE("Device profiles")))
		.Add(deviceField)
		.Add(profileScroll)
		.Add(new BStringView("params_title",
			B_TRANSLATE("Device parameters (PipeWire)")))
		.Add(fParamScroll)
		.Add(new BStringView("alsa_title",
			B_TRANSLATE("Raw ALSA controls (not exposed by PipeWire)")))
		.Add(fAlsaScroll)
		.Add(fStatus)
		.AddGlue();
}


HardwareSectionView::~HardwareSectionView()
{
}


void
HardwareSectionView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fDeviceMenu->SetTargetForItems(this);
	fProfileList->SetTarget(this);
	RefreshDevices();
}


void
HardwareSectionView::RefreshDevices()
{
	for (int32 i = fDeviceMenu->CountItems() - 1; i >= 0; i--)
		delete fDeviceMenu->RemoveItem(i);

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		fStatus->SetText(B_TRANSLATE("Audio backend unavailable"));
		return;
	}

	BObjectList<media_client_id, true> clients;
	if (g->GetClients(&clients) != B_OK)
		return;

	bool added = false;
	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (g->GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSink = false;
		bool isSource = false;
		info.FindBool("is.sink", &isSink);
		info.FindBool("is.source", &isSource);
		if (!isSink && !isSource)
			continue;
		BString name;
		if (info.FindString("name", &name) != B_OK)
			continue;

		BMenuItem* item = new BMenuItem(name.String(),
			new BMessage(kMsgHardwareDeviceSelected));
		item->Message()->AddInt32("device_id", id);
		if (isSink)
			fDeviceMenu->AddItem(item, 0);
		else
			fDeviceMenu->AddItem(item);
		added = true;
	}

	if (!added) {
		BMenuItem* none = new BMenuItem(
			B_TRANSLATE("No audio devices found"), NULL);
		none->SetEnabled(false);
		fDeviceMenu->AddItem(none);
		return;
	}

	BMenuItem* toMark = NULL;
	for (int32 i = 0; i < fDeviceMenu->CountItems(); i++) {
		BMenuItem* item = fDeviceMenu->ItemAt(i);
		int32 id = 0;
		if (item->Message() == NULL
				|| item->Message()->FindInt32("device_id", &id) != B_OK)
			continue;
		if ((media_client_id)id == fSelectedDevice) {
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
	if ((media_client_id)markedId != fSelectedDevice) {
		fSelectedDevice = (media_client_id)markedId;
		RefreshProfiles();
	}
}


void
HardwareSectionView::RefreshProfiles()
{
	fProfileList->RemoveItems(0, fProfileList->CountItems());
	fStatus->SetText("");
	fProfileIndices.clear();

	if (fSelectedDevice == 0) {
		fStatus->SetText(B_TRANSLATE("No device selected"));
		return;
	}

	BMediaGraph* g = BMediaGraph::Instance();
	if (g == NULL) {
		fStatus->SetText(B_TRANSLATE("Media graph unavailable"));
		return;
	}

	BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
	int32 activeIndex = -1;
	status_t status = g->GetDeviceProfiles(fSelectedDevice, &profiles,
		&activeIndex);
	if (status == B_NOT_SUPPORTED) {
		fStatus->SetText(B_TRANSLATE("No profiles reported"));
	} else if (status != B_OK) {
		fStatus->SetText(B_TRANSLATE("Failed to load profiles"));
	} else {
		fActiveProfile = activeIndex;
		for (int32 i = 0; i < profiles.CountItems(); i++) {
			BMediaGraph::DeviceProfileInfo* info = profiles.ItemAt(i);
			BString display = info->description;
			if (!info->available)
				display << " (" << B_TRANSLATE("unavailable") << ")";
			BStringItem* item = new BStringItem(display.String());
			fProfileList->AddItem(item);
			fProfileIndices.push_back(info->index);
			if (info->index == activeIndex)
				fProfileList->Select(i);
		}
	}

	_BuildPipeWireParams();
	_BuildAlsaControls();
}


void
HardwareSectionView::_BuildPipeWireParams()
{
	// The scroller keeps a raw target pointer, so swap the view in
	// before deleting the old one.
	BView* oldView = fParamView;

	BGroupView* paramGroup = new BGroupView("device_params", B_VERTICAL, 0);
	paramGroup->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fParamView = paramGroup;

	if (fSelectedDevice != 0
			&& fDeviceControllable.BindNode((uint32)fSelectedDevice) == B_OK) {
		BView* themeView = UnwrapThemeView(fDeviceControllable.MakeView());
		if (themeView != NULL) {
			themeView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
			paramGroup->GroupLayout()->AddView(themeView);
			paramGroup->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
		}
	}

	fParamScroll->SetTarget(fParamView);
	delete oldView;
	fParamScroll->InvalidateLayout();
}


void
HardwareSectionView::_ClearAlsaRows()
{
	BGroupLayout* layout = fAlsaGroup->GroupLayout();
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
HardwareSectionView::_BuildAlsaControls()
{
	_ClearAlsaRows();
	fAlsaList.clear();

	std::vector<AlsaControls::Control> controls;
	if (fAlsa.Fetch(controls) != B_OK || controls.empty()) {
		fAlsaGroup->GroupLayout()->AddView(new BStringView("alsa_empty",
			B_TRANSLATE("No extra controls on this system.")));
		return;
	}

	fAlsaList = controls;
	for (size_t i = 0; i < fAlsaList.size(); i++) {
		const AlsaControls::Control& control = fAlsaList[i];

		BString label = control.cardName;
		if (label.Length() == 0)
			label << "card" << control.cardIndex;
		label << ": " << control.name;

		BGroupView* row = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		row->GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
		BStringView* name = new BStringView("alsa_name", label.String());
		name->SetExplicitMinSize(BSize(be_plain_font->StringWidth(
			"WWWWWWWWWWWWWWWWWWWWWWWW"), B_SIZE_UNSET));
		row->GroupLayout()->AddView(name);

		if (control.isEnum) {
			BPopUpMenu* menu = new BPopUpMenu("enum_menu");
			for (size_t e = 0; e < control.enumNames.size(); e++) {
				BMessage* msg = new BMessage(kMsgAlsaValue);
				msg->AddInt32("row", (int32)i);
				msg->AddInt32("enum_index", (int32)e);
				BMenuItem* item = new BMenuItem(
					control.enumNames[e].String(), msg);
				if ((int32)e == control.enumIndex)
					item->SetMarked(true);
				menu->AddItem(item);
			}
			menu->SetTargetForItems(this);
			BMenuField* field = new BMenuField("alsa_enum", NULL, menu);
			row->GroupLayout()->AddView(field);
		} else {
			BSlider* slider = new BSlider("alsa_value", NULL,
				new BMessage(kMsgAlsaValue), 0, 100, B_HORIZONTAL,
				B_BLOCK_THUMB, B_NAVIGABLE | B_WILL_DRAW);
			BMessage* modify = new BMessage(kMsgAlsaValue);
			modify->AddInt32("row", (int32)i);
			slider->SetModificationMessage(modify);
			BMessage* invocation = new BMessage(kMsgAlsaValue);
			invocation->AddInt32("row", (int32)i);
			slider->SetMessage(invocation);
			slider->SetValue((int32)(control.value * 100.0f + 0.5f));
			slider->SetExplicitMinSize(BSize(120, B_SIZE_UNSET));
			row->GroupLayout()->AddView(slider, 1.0f);
		}

		BMessage* muteMsg = new BMessage(kMsgAlsaMute);
		muteMsg->AddInt32("row", (int32)i);
		BCheckBox* mute = new BCheckBox("alsa_mute", B_TRANSLATE("Mute"),
			muteMsg);
		mute->SetValue(control.muted ? B_CONTROL_ON : B_CONTROL_OFF);
		row->GroupLayout()->AddView(mute);

		fAlsaGroup->GroupLayout()->AddView(row);
	}
}


void
HardwareSectionView::NotifyChanged(uint32 what, uint32 deviceId)
{
	if (what == BMediaGraph::B_MEDIA_CLIENT_ADDED
			|| what == BMediaGraph::B_MEDIA_CLIENT_REMOVED) {
		RefreshDevices();
		return;
	}
	if (fSelectedDevice != 0 && deviceId != 0 && deviceId != (uint32)fSelectedDevice)
		return;

	if (what == kMsgDeviceProfileChanged || what == kMsgDevicePortChanged) {
		RefreshProfiles();
	} else if (what == BMediaGraph::B_MEDIA_VOLUME_CHANGED) {
		_BuildPipeWireParams();
	}
}


void
HardwareSectionView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgHardwareDeviceSelected:
		{
			int32 id = 0;
			if (msg->FindInt32("device_id", &id) != B_OK)
				break;
			fSelectedDevice = (media_client_id)id;
			RefreshProfiles();
			break;
		}

		case kMsgHardwareProfileSelected:
		{
			int32 row = fProfileList->CurrentSelection();
			if (row < 0 || row >= (int32)fProfileIndices.size())
				break;
			int32 profileIndex = fProfileIndices[row];
			if (profileIndex == fActiveProfile)
				break;

			BMediaGraph* g = BMediaGraph::Instance();
			status_t status = g != NULL
				? g->SetDeviceProfile(fSelectedDevice, profileIndex)
				: B_DEVICE_NOT_FOUND;
			RefreshProfiles();
			if (status != B_OK)
				fStatus->SetText(B_TRANSLATE("Failed to switch profile"));
			break;
		}

		case kMsgAlsaValue:
		{
			int32 row = -1;
			if (msg->FindInt32("row", &row) != B_OK
					|| row < 0 || row >= (int32)fAlsaList.size())
				break;
			AlsaControls::Control control = fAlsaList[row];
			if (control.isEnum) {
				int32 index = -1;
				if (msg->FindInt32("enum_index", &index) == B_OK) {
					control.enumIndex = index;
					fAlsa.Apply(control, control.value, control.muted);
				}
				break;
			}

			// The slider lives in the same row as the name; find it by
			// walking back from the sender's row group.
			float value = control.value;
			void* source = NULL;
			if (msg->FindPointer("be:source", &source) == B_OK) {
				BView* view = (BView*)source;
				while (view != NULL && value == control.value) {
					BSlider* slider = dynamic_cast<BSlider*>(view);
					if (slider != NULL) {
						value = slider->Value() / 100.0f;
						break;
					}
					view = view->Parent();
				}
			}
			control.value = value;
			fAlsaList[row] = control;
			fAlsa.Apply(control, value, control.muted);
			break;
		}

		case kMsgAlsaMute:
		{
			int32 row = -1;
			if (msg->FindInt32("row", &row) != B_OK
					|| row < 0 || row >= (int32)fAlsaList.size())
				break;
			AlsaControls::Control control = fAlsaList[row];
			void* source = NULL;
			bool mute = !control.muted;
			if (msg->FindPointer("be:source", &source) == B_OK) {
				BView* view = (BView*)source;
				while (view != NULL) {
					BCheckBox* box = dynamic_cast<BCheckBox*>(view);
					if (box != NULL) {
						mute = box->Value() == B_CONTROL_ON;
						break;
					}
					view = view->Parent();
				}
			}
			control.muted = mute;
			fAlsaList[row] = control;
			fAlsa.Apply(control, control.value, mute);
			break;
		}

		default:
			BGroupView::MessageReceived(msg);
			break;
	}
}
