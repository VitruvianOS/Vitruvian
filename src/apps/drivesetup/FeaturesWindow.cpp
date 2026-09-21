/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */


#include "FeaturesWindow.h"

#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <String.h>

#include "PartitionCapabilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FeaturesWindow"


// Must match _kMustTier in vos-install-helper exactly.
static const char* const kMustTierFilesystems[] = {
	"intel", "gpt", "ext4", "fat", "xfs", "btrfs", "linux-swap", "luks"
};
static const int32 kMustTierCount = 8;


enum {
	kFilesystemColumn = 0,
	kCreateColumn,
	kGrowColumn,
	kShrinkColumn,
	kMoveColumn,
	kCheckColumn,
	kReadLabelColumn,
	kWriteLabelColumn,
	kReadUuidColumn,
	kMissingToolsColumn
};


static BString
	online_suffix(const BString& value, bool online)
{
	if (online && value == "external")
		return BString(value) << " (" << B_TRANSLATE("online") << ")";
	return value;
}


FeaturesWindow::FeaturesWindow(BWindow* window)
	:
	BWindow(BRect(80, 80, 760, 380), B_TRANSLATE("Filesystem features"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fListView(NULL)
{
	if (window != NULL)
		AddToSubset(window);

	fListView = new BColumnListView("features", B_SUPPORTS_LAYOUT,
		B_FANCY_BORDER, true);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("File system"), 90,
		50, 200, B_TRUNCATE_END), kFilesystemColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Create"), 70, 50,
		150, B_TRUNCATE_END), kCreateColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Grow"), 90, 50, 150,
		B_TRUNCATE_END), kGrowColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Shrink"), 90, 50,
		150, B_TRUNCATE_END), kShrinkColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Move"), 70, 50, 150,
		B_TRUNCATE_END), kMoveColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Check"), 70, 50, 150,
		B_TRUNCATE_END), kCheckColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Read label"), 80, 50,
		150, B_TRUNCATE_END), kReadLabelColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Write label"), 80,
		50, 150, B_TRUNCATE_END), kWriteLabelColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Read UUID"), 80, 50,
		150, B_TRUNCATE_END), kReadUuidColumn);
	fListView->AddColumn(new BStringColumn(B_TRANSLATE("Missing tools"), 220,
		80, 500, B_TRUNCATE_END), kMissingToolsColumn);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fListView);

	_Populate();
}


FeaturesWindow::~FeaturesWindow()
{
}


bool
FeaturesWindow::QuitRequested()
{
	return true;
}


void
FeaturesWindow::_Populate()
{
	for (int32 i = 0; i < kMustTierCount; i++) {
		const char* fs = kMustTierFilesystems[i];

		PartitionCapabilities caps;
		BRow* row = new BRow();

		if (PartitionCapabilities::Get(fs, caps) == B_OK) {
			row->SetField(new BStringField(fs), kFilesystemColumn);
			row->SetField(new BStringField(caps.create.String()),
				kCreateColumn);
			row->SetField(new BStringField(
				online_suffix(caps.grow, caps.onlineGrow).String()),
				kGrowColumn);
			row->SetField(new BStringField(
				online_suffix(caps.shrink, caps.onlineShrink).String()),
				kShrinkColumn);
			row->SetField(new BStringField(caps.move.String()),
				kMoveColumn);
			row->SetField(new BStringField(caps.check.String()),
				kCheckColumn);
			row->SetField(new BStringField(caps.readLabel.String()),
				kReadLabelColumn);
			row->SetField(new BStringField(caps.writeLabel.String()),
				kWriteLabelColumn);
			row->SetField(new BStringField(caps.readUuid.String()),
				kReadUuidColumn);

			BString missing;
			for (int32 t = 0; t < caps.toolsMissing.CountStrings(); t++) {
				if (t > 0)
					missing << ", ";
				missing << caps.toolsMissing.StringAt(t);
			}
			row->SetField(new BStringField(missing.String()),
				kMissingToolsColumn);
		} else {
			row->SetField(new BStringField(fs), kFilesystemColumn);
			BString error(B_TRANSLATE("(could not query capabilities)"));
			row->SetField(new BStringField(error.String()), kCreateColumn);
		}

		fListView->AddRow(row);
	}
}
