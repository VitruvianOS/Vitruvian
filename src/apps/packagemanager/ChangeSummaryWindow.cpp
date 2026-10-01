/*
 * Copyright 2026, Angelo Scarnà. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ChangeSummaryWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>

#include "PackageManagerDefs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ChangeSummaryWindow"


// A group header ("To be installed", ...) that stands out from its
// children. BStringItem draws its own text, so bolding it just means
// swapping in the bold system font for the draw.
class GroupItem : public BStringItem {
public:
	GroupItem(const char* text)
		:
		BStringItem(text, 0, true)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		owner->SetFont(be_bold_font);
		BStringItem::DrawItem(owner, frame, complete);
		owner->SetFont(be_plain_font);
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, be_bold_font);
	}
};


ChangeSummaryWindow::ChangeSummaryWindow(BWindow* parent,
	const BMessenger& target, const BMessage* details, const char* summary)
	:
	Inherited(BRect(0, 0, 480, 420), B_TRANSLATE("Apply these changes?"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fConfirmed(false)
{
	if (parent != NULL) {
		SetFeel(B_MODAL_SUBSET_WINDOW_FEEL);
		AddToSubset(parent);
	}

	fListView = new BOutlineListView("changes", B_SINGLE_SELECTION_LIST);

	int32 newCount = 0, upgradeCount = 0, removeCount = 0;
	if (details != NULL) {
		removeCount = _AddGroup(details, "remove", B_TRANSLATE("To be removed"));
		upgradeCount = _AddGroup(details, "upgrade",
			B_TRANSLATE("To be upgraded"));
		newCount = _AddGroup(details, "new", B_TRANSLATE("To be installed"));
	}

	BString headline;
	if (newCount + upgradeCount + removeCount == 0) {
		headline = B_TRANSLATE("No further package changes are required.");
	} else {
		headline = B_TRANSLATE(
			"%install% to install, %upgrade% to upgrade, %remove% to remove.");
		BString value;
		value << newCount;
		headline.ReplaceFirst("%install%", value);
		value.SetTo("");
		value << upgradeCount;
		headline.ReplaceFirst("%upgrade%", value);
		value.SetTo("");
		value << removeCount;
		headline.ReplaceFirst("%remove%", value);
	}

	BStringView* headlineView = new BStringView("headline", headline.String());
	headlineView->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

	// apt already worked out the on-disk delta; surface its own wording
	// rather than re-deriving it.
	BString spaceLine = _FindSummaryLine(summary, "After this operation");
	BStringView* spaceView = new BStringView("space", spaceLine.String());
	spaceView->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

	BScrollView* scrollView = new BScrollView("scroll", fListView, 0, false,
		true);
	scrollView->SetExplicitMinSize(BSize(320, 220));

	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	BButton* applyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(kMsgSummaryApply));
	applyButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(headlineView)
		.AddStrut(B_USE_HALF_ITEM_SPACING)
		.Add(scrollView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(spaceView)
			.AddGlue()
			.Add(cancelButton)
			.Add(applyButton)
		.End()
	.End();

	if (parent != NULL)
		CenterIn(parent->Frame());
	else
		CenterOnScreen();
}


ChangeSummaryWindow::~ChangeSummaryWindow()
{
}


void
ChangeSummaryWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSummaryApply:
			fConfirmed = true;
			fTarget.SendMessage(kMsgSummaryApply);
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			Inherited::MessageReceived(message);
			break;
	}
}


bool
ChangeSummaryWindow::QuitRequested()
{
	if (!fConfirmed)
		fTarget.SendMessage(kMsgSummaryCancel);
	return true;
}


int32
ChangeSummaryWindow::_AddGroup(const BMessage* details, const char* field,
	const char* label)
{
	type_code type;
	int32 count = 0;
	if (details->GetInfo(field, &type, &count) != B_OK || count == 0)
		return 0;

	BString groupLabel(label);
	groupLabel << " (" << count << ")";
	BStringItem* group = new GroupItem(groupLabel.String());
	fListView->AddItem(group);

	for (int32 i = 0; i < count; i++) {
		const char* entry = NULL;
		if (details->FindString(field, i, &entry) != B_OK)
			continue;

		BString name(entry);
		BString version;
		int32 tab = name.FindFirst('\t');
		if (tab >= 0) {
			name.CopyInto(version, tab + 1, name.Length() - tab - 1);
			name.Truncate(tab);
		}

		BString text(name);
		if (version.Length() > 0)
			text << "  (" << version << ")";

		BStringItem* item = new BStringItem(text.String(), 1);
		fListView->AddUnder(item, group);
	}

	fListView->Expand(group);
	return count;
}


BString
ChangeSummaryWindow::_FindSummaryLine(const char* summary, const char* needle)
{
	if (summary == NULL || needle == NULL)
		return BString();

	BString text(summary);
	int32 start = 0;
	while (start <= text.Length()) {
		int32 newline = text.FindFirst('\n', start);
		int32 end = newline < 0 ? text.Length() : newline;
		BString line;
		text.CopyInto(line, start, end - start);
		if (line.IFindFirst(needle) >= 0) {
			line.Trim();
			return line;
		}
		if (newline < 0)
			break;
		start = newline + 1;
	}
	return BString();
}
