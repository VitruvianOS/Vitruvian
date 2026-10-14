/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceTreeView.h"

#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>

#include <utility>

#include "DeviceClassifier.h"
#include "DeviceListItem.h"
#include "HardwareMessages.h"
#include "HardwareReport.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


DeviceTreeView::DeviceTreeView(Hardware::DeviceTab tab,
	const BMessenger& parent)
	:
	BView("device tree", B_WILL_DRAW | B_FRAME_EVENTS),
	fTab(tab),
	fParent(parent),
	fNames(NULL)
{
	fList = new BOutlineListView("devices", B_SINGLE_SELECTION_LIST);
	fList->SetTarget(this);
	fList->SetSelectionMessage(new BMessage(kMsgListSelection));

	fDetail = new BTextView("details", B_WILL_DRAW | B_PULSE_NEEDED);
	fDetail->MakeEditable(false);
	fDetail->SetFontAndColor(be_fixed_font, B_FONT_ALL,
		&ui_color(B_LIST_ITEM_TEXT_COLOR));
	fDetail->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));

	fAdvanced = new BTextView("advanced", B_WILL_DRAW | B_PULSE_NEEDED);
	fAdvanced->MakeEditable(false);
	fAdvanced->SetFontAndColor(be_fixed_font, B_FONT_ALL,
		&ui_color(B_LIST_ITEM_TEXT_COLOR));
	fAdvanced->SetViewColor(ui_color(B_LIST_BACKGROUND_COLOR));

	fAdvancedCaption = new BStringView("advanced caption",
		B_TRANSLATE("Advanced (udev properties)"));

	BScrollView* detailScroll = new BScrollView("details scroll",
		fDetail, B_WILL_DRAW | B_FRAME_EVENTS, false, true);
	BScrollView* advancedScroll = new BScrollView("advanced scroll",
		fAdvanced, B_WILL_DRAW | B_FRAME_EVENTS, false, true);

	BView* advancedGroup = new BView("advanced group",
		B_WILL_DRAW | B_FRAME_EVENTS);
	BLayoutBuilder::Group<>(advancedGroup, B_VERTICAL, 0)
		.Add(fAdvancedCaption)
		.Add(advancedScroll)
		.SetInsets(0, 0, 0, 0);

	BSplitView* rightSplit = new BSplitView(B_VERTICAL);
	rightSplit->AddChild(detailScroll, 3.0);
	rightSplit->AddChild(advancedGroup, 2.0);

	BSplitView* mainSplit = new BSplitView(B_HORIZONTAL);
	mainSplit->AddChild(fList, 1.0);
	mainSplit->AddChild(rightSplit, 2.0);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(mainSplit)
		.SetInsets(0);
}


void
DeviceTreeView::SetRecords(
	const std::vector<Hardware::DeviceRecord>& records,
	const Hardware::HwdataDatabase* names)
{
	fRecords = records;
	fNames = names;
	_Rebuild();
	_UpdateDetails();
}


const Hardware::DeviceRecord*
DeviceTreeView::SelectedRecord() const
{
	int32 index = fList->CurrentSelection();
	if (index < 0)
		return NULL;

	return _ItemRecord(fList->ItemAt(index));
}


std::string
DeviceTreeView::SelectedDetail(bool includeProperties) const
{
	const Hardware::DeviceRecord* record = SelectedRecord();
	if (record == NULL)
		return std::string();

	return Hardware::BuildDeviceDetail(*record, _Names(),
		includeProperties);
}


void
DeviceTreeView::SetPrivilegedText(const std::string& text)
{
	fPrivilegedText = text;
	_UpdateDetails();
}


void
DeviceTreeView::ShowEmptyState()
{
	BString text;
	if (fTab == Hardware::kTabPlugPlay)
		text << B_TRANSLATE("No discoverable hardware was found.\n\n"
			"The Plug and Play tab lists PCI, USB, display, "
			"network, input and similar devices reported by the "
			"kernel.");
	else
		text << B_TRANSLATE("No platform devices were found.\n\n"
			"The Platform tab lists devices described by the "
			"device tree or ACPI: I2C, SPI, GPIO, sensors, "
			"panels and similar. On a PC without device tree "
			"support this tab only shows ACPI platform devices "
			"with a driver.");

	fDetail->SetText(text.String());
	fAdvanced->SetText("");
}


void
DeviceTreeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgListSelection:
			_UpdateDetails();
			if (fParent.IsValid()) {
				BMessage notice(kMsgListSelection);
				fParent.SendMessage(&notice);
			}
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


const Hardware::HwdataDatabase&
DeviceTreeView::_Names() const
{
	return fNames != NULL ? *fNames : fEmptyNames;
}


const Hardware::DeviceRecord*
DeviceTreeView::_ItemRecord(BListItem* item) const
{
	DeviceItem* deviceItem = dynamic_cast<DeviceItem*>(item);
	if (deviceItem == NULL)
		return NULL;

	return &deviceItem->Record();
}


void
DeviceTreeView::_Rebuild()
{
	fList->MakeEmpty();
	fSelectedSyspath = std::string();

	// Bucket the records of this tab by category, in enum order.
	const int32 kCategoryCount = Hardware::kCategoryRemoteProc + 1;
	std::vector<const Hardware::DeviceRecord*> buckets[kCategoryCount];

	for (size_t i = 0; i < fRecords.size(); i++) {
		Hardware::Classification classification
			= Hardware::ClassifyRecord(fRecords[i]);
		if (!classification.wanted || classification.tab != fTab)
			continue;
		buckets[classification.category].push_back(&fRecords[i]);
	}

	int32 deviceCount = 0;
	for (int32 c = 0; c < kCategoryCount; c++) {
		if (buckets[c].empty())
			continue;

		DeviceCategoryItem* categoryItem = new DeviceCategoryItem(
			(Hardware::DeviceCategory)c);
		fList->AddItem(categoryItem);

		std::vector<std::pair<std::string,
			const Hardware::DeviceRecord*> > sorted;
		for (size_t i = 0; i < buckets[c].size(); i++) {
			std::string name = Hardware::DeviceDisplayName(
				*buckets[c][i], _Names());
			sorted.push_back(std::make_pair(name, buckets[c][i]));
		}
		// Insertion sort; the buckets stay small.
		for (size_t i = 1; i < sorted.size(); i++) {
			for (size_t j = i; j > 0
				&& sorted[j].first < sorted[j - 1].first; j--)
				std::swap(sorted[j], sorted[j - 1]);
		}

		for (size_t i = 0; i < sorted.size(); i++) {
			fList->AddItem(new DeviceItem(*sorted[i].second,
				sorted[i].first.c_str()), -1);
			deviceCount++;
		}

		fList->Expand(categoryItem);
	}

	if (deviceCount == 0)
		ShowEmptyState();
}


void
DeviceTreeView::_UpdateDetails()
{
	const Hardware::DeviceRecord* record = SelectedRecord();
	if (record == NULL) {
		if (fList->FullListIsEmpty())
			ShowEmptyState();
		else {
			fDetail->SetText(B_TRANSLATE("Select a device to see "
				"its properties."));
			fAdvanced->SetText("");
		}
		return;
	}

	std::string text = Hardware::BuildDeviceDetail(*record, _Names(),
		false);

	if (!fPrivilegedText.empty()) {
		text += "\n";
		text += B_TRANSLATE("Privileged details (administrator "
			"assisted):\n");
		text += fPrivilegedText;
	}

	fDetail->SetText(text.c_str());

	std::string advanced;
	for (size_t i = 0; i < record->properties.size(); i++) {
		advanced += record->properties[i].key;
		advanced += "=";
		advanced += record->properties[i].value;
		advanced += "\n";
	}
	fAdvanced->SetText(advanced.c_str());
}
