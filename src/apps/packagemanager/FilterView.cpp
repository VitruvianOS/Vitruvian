/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FilterView.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <ObjectList.h>
#include <TextControl.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FilterView"


static BMenuItem*
status_item(const char* label, status_filter status)
{
	BMessage* message = new BMessage(kMsgStatusSelected);
	message->AddInt32("status", (int32)status);
	return new BMenuItem(label, message);
}


static int
compare_sections(const BString* a, const BString* b)
{
	return a->Compare(*b);
}


FilterView::FilterView()
	:
	Inherited("filter view", B_HORIZONTAL, B_USE_SMALL_SPACING),
	fStatus(kFilterAll)
{
	fSearchControl = new BTextControl("search", B_TRANSLATE("Search:"), "",
		NULL);
	fSearchControl->SetModificationMessage(new BMessage(kMsgFilterChanged));

	fSectionMenu = new BMenu(B_TRANSLATE("All categories"));
	fSectionMenu->SetRadioMode(true);
	fSectionMenu->SetLabelFromMarked(true);
	BMenuItem* allCategoriesItem = new BMenuItem(B_TRANSLATE("All categories"),
		new BMessage(kMsgSectionSelected));
	fSectionMenu->AddItem(allCategoriesItem);
	allCategoriesItem->SetMarked(true);
	fSectionField = new BMenuField("section", "", fSectionMenu);

	fStatusMenu = new BMenu(B_TRANSLATE("All packages"));
	fStatusMenu->SetRadioMode(true);
	fStatusMenu->SetLabelFromMarked(true);
	BMenuItem* allPackagesItem = status_item(B_TRANSLATE("All packages"),
		kFilterAll);
	fStatusMenu->AddItem(allPackagesItem);
	fStatusMenu->AddItem(status_item(B_TRANSLATE("Installed"),
		kFilterInstalled));
	fStatusMenu->AddItem(status_item(B_TRANSLATE("Upgradable"),
		kFilterUpgradable));
	fStatusMenu->AddItem(status_item(B_TRANSLATE("Marked"), kFilterMarked));
	allPackagesItem->SetMarked(true);
	fStatusField = new BMenuField("status", "", fStatusMenu);

	BLayoutBuilder::Group<>(this)
		.Add(fSearchControl)
		.Add(fSectionField)
		.Add(fStatusField)
		.AddGlue();
}


FilterView::~FilterView()
{
}


void
FilterView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	fSearchControl->SetTarget(Window());
	fSectionMenu->SetTargetForItems(Window());
	fStatusMenu->SetTargetForItems(Window());
}


BString
FilterView::SearchText() const
{
	return BString(fSearchControl->Text());
}


void
FilterView::SetSections(const BObjectList<BString, true>& sections)
{
	BObjectList<BString> sorted(sections.CountItems());
	for (int32 i = 0; i < sections.CountItems(); i++)
		sorted.AddItem(sections.ItemAt(i));
	sorted.SortItems(compare_sections);

	fSectionMenu->RemoveItems(0, fSectionMenu->CountItems(), true);

	BMenuItem* allItem = new BMenuItem(B_TRANSLATE("All categories"),
		new BMessage(kMsgSectionSelected));
	fSectionMenu->AddItem(allItem);

	BMenuItem* selected = allItem;
	for (int32 i = 0; i < sorted.CountItems(); i++) {
		const BString& name = *sorted.ItemAt(i);
		BMessage* message = new BMessage(kMsgSectionSelected);
		message->AddString("section", name);
		BMenuItem* item = new BMenuItem(name, message);
		fSectionMenu->AddItem(item);
		if (fSection == name)
			selected = item;
	}

	if (selected == allItem)
		fSection = "";

	selected->SetMarked(true);

	if (Window() != NULL)
		fSectionMenu->SetTargetForItems(Window());
}


void
FilterView::SetSection(const char* section)
{
	fSection = section;
}


void
FilterView::SetStatus(status_filter status)
{
	fStatus = status;
}
