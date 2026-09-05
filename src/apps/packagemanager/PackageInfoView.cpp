/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageInfoView.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StringForSize.h>
#include <StringItem.h>
#include <TabView.h>
#include <TextView.h>
#include <Window.h>

#include "PackageInfo.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageInfoView"


static BTextView*
make_read_only_text_view(const char* name)
{
	BTextView* view = new BTextView(name);
	view->MakeEditable(false);
	view->SetStylable(true);
	view->SetWordWrap(true);
	view->SetInsets(6, 6, 6, 6);
	return view;
}


PackageInfoView::PackageInfoView()
	:
	Inherited("package info", B_VERTICAL, B_USE_SMALL_SPACING),
	fPackage(NULL),
	fTransactionActive(false)
{
	fTitleView = new TruncatingStringView("title", "");
	fTitleView->SetFont(be_bold_font);
	fTitleView->SetMinWidthChars(24);
	fVersionView = new TruncatingStringView("version", "");
	fVersionView->SetMinWidthChars(16);
	fChannelView = new TruncatingStringView("channel", "");
	fChannelView->SetMinWidthChars(12);

	// The label and target message are re-derived per package; see
	// _UpdateSelectButton().
	fSelectButton = new BButton("select", B_TRANSLATE("Select"),
		new BMessage(kMsgMarkInstall));
	fSelectButton->SetEnabled(false);

	fApplyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(kMsgApply));
	fApplyButton->SetEnabled(false);

	fDescriptionView = make_read_only_text_view("description");
	fContentsView = new BOutlineListView("contents");
	fChangelogView = make_read_only_text_view("changelog");

	fTabView = new BTabView("detail tabs", B_WIDTH_FROM_LABEL);
	fTabView->AddTab(new BScrollView("description scroll", fDescriptionView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(0)->SetLabel(B_TRANSLATE("Description"));
	fTabView->AddTab(new BScrollView("contents scroll", fContentsView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(1)->SetLabel(B_TRANSLATE("Contents"));
	fTabView->AddTab(new BScrollView("changelog scroll", fChangelogView,
		0, false, true, B_NO_BORDER));
	fTabView->TabAt(2)->SetLabel(B_TRANSLATE("Changelog"));

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fTitleView)
			.Add(fVersionView)
			.Add(fChannelView)
			.AddGlue()
			.Add(fSelectButton)
			.Add(fApplyButton)
		.End()
		.Add(fTabView);

	Clear();
}


PackageInfoView::~PackageInfoView()
{
}


void
PackageInfoView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	fSelectButton->SetTarget(Window());
	fApplyButton->SetTarget(Window());
}


void
PackageInfoView::SetPackage(PackageInfo* package)
{
	fPackage = package;
	if (package == NULL) {
		Clear();
		return;
	}

	fTitleView->SetFullText(package->Name().String());
	fVersionView->SetFullText(package->Version().String());
	fChannelView->SetFullText(package->ChannelLabel());
	fDescriptionView->SetText(package->Summary().String());
	// Replies land later; drop the previous package's data now.
	fContentsView->MakeEmpty();
	fChangelogView->SetText("");
	_UpdateSelectButton();
}


void
PackageInfoView::SetDetails(const BMessage* details)
{
	if (details == NULL)
		return;

	// Drop a stale reply for a since-deselected package.
	const char* name = NULL;
	if (details->FindString("name", &name) != B_OK || fPackage == NULL
			|| fPackage->Name() != name) {
		return;
	}

	const char* description = "";
	details->FindString("description", &description);
	const char* version = "";
	details->FindString("version", &version);
	const char* section = "";
	details->FindString("section", &section);
	int64 installedSize = 0;
	details->FindInt64("installed_size", &installedSize);
	const char* depends = "";
	details->FindString("depends", &depends);

	char sizeText[64];
	string_for_size((double)installedSize, sizeText, sizeof(sizeText));

	BString text(description);
	text << "\n\n";
	text << B_TRANSLATE("Version: ") << version << "\n";
	text << B_TRANSLATE("Section: ") << section << "\n";
	text << B_TRANSLATE("Installed size: ") << sizeText << "\n";
	if (BString(depends).Length() > 0)
		text << B_TRANSLATE("Depends: ") << depends << "\n";
	fDescriptionView->SetText(text.String());

	fContentsView->MakeEmpty();
	type_code type;
	int32 count = 0;
	if (details->GetInfo("path", &type, &count) == B_OK) {
		for (int32 i = 0; i < count; i++) {
			const char* path = NULL;
			if (details->FindString("path", i, &path) == B_OK)
				fContentsView->AddItem(new BStringItem(path));
		}
	}
}


void
PackageInfoView::SetChangelog(const BMessage* changelog)
{
	if (changelog == NULL)
		return;

	const char* name = NULL;
	if (changelog->FindString("name", &name) != B_OK || fPackage == NULL
			|| fPackage->Name() != name) {
		return;
	}

	const char* text = "";
	changelog->FindString("text", &text);
	fChangelogView->SetText(text);
}


void
PackageInfoView::Clear()
{
	fTitleView->SetFullText(B_TRANSLATE("No package selected"));
	fVersionView->SetFullText("");
	fChannelView->SetFullText("");
	fDescriptionView->SetText("");
	fChangelogView->SetText("");
	fContentsView->MakeEmpty();
	_UpdateSelectButton();
}


void
PackageInfoView::SetTransactionActive(bool active)
{
	fTransactionActive = active;
	_UpdateSelectButton();
}


void
PackageInfoView::RefreshSelectButton()
{
	_UpdateSelectButton();
}


void
PackageInfoView::SetApplyState(int32 pendingCount, bool enabled)
{
	if (pendingCount > 0) {
		BString label(B_TRANSLATE("Apply %count% changes"));
		BString countText;
		countText << pendingCount;
		label.ReplaceFirst("%count%", countText);
		fApplyButton->SetLabel(label.String());
	} else {
		fApplyButton->SetLabel(B_TRANSLATE("Apply"));
	}

	fApplyButton->SetEnabled(enabled);
}


void
PackageInfoView::_UpdateSelectButton()
{
	if (fPackage == NULL || fTransactionActive) {
		fSelectButton->SetLabel(B_TRANSLATE("Select"));
		fSelectButton->SetEnabled(false);
		return;
	}

	// A mark always wins: Deselect only clears the pending mark, it
	// never touches the system. Unmarked: State() picks the action;
	// upgradable still posts kMarkInstall, apt's install verb performs
	// the upgrade.
	if (fPackage->Mark() != kMarkNone) {
		fSelectButton->SetLabel(B_TRANSLATE("Deselect"));
		fSelectButton->SetMessage(new BMessage(kMsgUnmark));
	} else if (fPackage->State() == kPackageInstalled) {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Uninstall"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkRemove));
	} else if (fPackage->State() == kPackageUpgradable) {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Update"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkInstall));
	} else {
		fSelectButton->SetLabel(B_TRANSLATE("Select for Install"));
		fSelectButton->SetMessage(new BMessage(kMsgMarkInstall));
	}

	fSelectButton->SetEnabled(true);
}
