/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageListView.h"

#include <Catalog.h>
#include <List.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Window.h>

#include "PackageInfo.h"
#include "PackageRow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageListView"


static const int32 kRebuildThreshold = 2000;


// The icon column's bitmap doubles as a toggle: click an empty box to
// mark install, an installed one to mark removal, an upgradable one to
// mark the upgrade (still kMarkInstall), any marked box to clear. Purge
// is menu-only.
class MarkColumn : public BBitmapColumn {
public:
	MarkColumn(const char* title, float width, float minWidth, float maxWidth,
		alignment align)
		:
		BBitmapColumn(title, width, minWidth, maxWidth, align)
	{
		SetWantsEvents(true);
	}

	virtual void MouseDown(BColumnListView* parent, BRow* row, BField* field,
		BRect fieldRect, BPoint point, uint32 buttons)
	{
		if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
			return;

		PackageListView* listView = dynamic_cast<PackageListView*>(parent);
		if (listView == NULL || listView->TransactionActive())
			return;

		PackageRow* packageRow = dynamic_cast<PackageRow*>(row);
		if (packageRow == NULL)
			return;
		PackageInfo* package = packageRow->Package();
		if (package == NULL)
			return;

		// kMarkInstall on an upgradable package IS the upgrade; only an
		// actually-installed package defaults to a removal offer.
		if (package->Mark() != kMarkNone)
			package->SetMark(kMarkNone);
		else if (package->State() == kPackageInstalled)
			package->SetMark(kMarkRemove);
		else
			package->SetMark(kMarkInstall);

		packageRow->Refresh();
		listView->UpdateRow(packageRow);

		if (listView->Window() != NULL)
			listView->Window()->PostMessage(kMsgGlyphMarkChanged);
	}
};


// Name alone is not unique under multiarch.
static HashString
row_key(const PackageInfo* package)
{
	BString key(package->Name());
	key << '\t' << package->Architecture();
	return HashString(key.String());
}


PackageListView::PackageListView()
	:
	Inherited("packages", B_NAVIGABLE, B_FANCY_BORDER, true),
	fContextMenu(NULL),
	fTransactionActive(false)
{
	// Must precede _AddColumns(): SetSortColumn() is a no-op while
	// sorting is disabled, and SetSortingEnabled() would wipe the
	// columns it registers.
	SetSortingEnabled(true);
	_AddColumns();
	SetSelectionMode(B_MULTIPLE_SELECTION_LIST);

	fContextMenu = new BPopUpMenu("package", false, false);
	fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Install"),
		new BMessage(kMsgMarkInstall)));
	fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Remove"),
		new BMessage(kMsgMarkRemove)));
	fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Purge"),
		new BMessage(kMsgMarkPurge)));
	fContextMenu->AddSeparatorItem();
	fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Clear mark"),
		new BMessage(kMsgUnmark)));
}


PackageListView::~PackageListView()
{
	delete fContextMenu;
}


void
PackageListView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	SetSelectionMessage(new BMessage(kMsgPackageSelected));
	SetTarget(Window());
	fContextMenu->SetTargetForItems(Window());
}


void
PackageListView::MouseDown(BPoint where)
{
	BMessage* message = Window()->CurrentMessage();
	int32 buttons = 0;
	bool isContextClick = message != NULL
		&& message->FindInt32("buttons", &buttons) == B_OK
		&& (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

	if (!isContextClick) {
		Inherited::MouseDown(where);
		return;
	}

	// Preserve a multi-row selection across a right click inside it; the
	// base class would otherwise collapse it to the clicked row.
	BList previousSelection;
	BRow* clickedRow = RowAt(where);
	bool preserveSelection = clickedRow != NULL && clickedRow->IsSelected();
	if (preserveSelection) {
		BRow* row = NULL;
		while ((row = CurrentSelection(row)) != NULL)
			previousSelection.AddItem(row);
	}

	Inherited::MouseDown(where);

	if (preserveSelection && previousSelection.CountItems() > 1) {
		for (int32 i = 0; i < previousSelection.CountItems(); i++)
			AddToSelection((BRow*)previousSelection.ItemAt(i));
	}

	PackageInfo* package = SelectedPackage();
	if (package == NULL)
		return;

	BMenuItem* item;

	item = fContextMenu->ItemAt(0);
	if (item != NULL)
		item->SetEnabled(package->State() != kPackageInstalled);

	item = fContextMenu->ItemAt(1);
	if (item != NULL)
		item->SetEnabled(package->State() != kPackageAvailable);

	item = fContextMenu->ItemAt(2);
	if (item != NULL)
		item->SetEnabled(package->State() != kPackageAvailable);

	item = fContextMenu->ItemAt(4);
	if (item != NULL)
		item->SetEnabled(package->Mark() != kMarkNone);

	BPoint screenPoint = ConvertToScreen(where);
	fContextMenu->Go(screenPoint, true, true, false);
}


void
PackageListView::SetPackages(const BObjectList<PackageInfo, true>& packages,
	const BString& filter, const BString& section, status_filter status)
{
	HashMap<HashString, PackageInfo*> wanted;
	for (int32 i = 0; i < packages.CountItems(); i++) {
		PackageInfo* package = packages.ItemAt(i);
		if (_Matches(package, filter, section, status))
			wanted.Put(row_key(package), package);
	}

	// RemoveRows() is O(rows) per row; past a threshold, rebuilding wins.
	int32 doomed = 0;
	const int32 rowCount = CountRows();
	for (int32 i = 0; i < rowCount; i++) {
		PackageRow* row = dynamic_cast<PackageRow*>(RowAt(i));
		if (row == NULL || row->Package() == NULL)
			continue;
		if (!wanted.ContainsKey(row_key(row->Package())))
			doomed++;
	}

	if (doomed > kRebuildThreshold || doomed > rowCount / 2) {
		ClearPackages();
	} else if (doomed > 0) {
		BList removedRows;
		for (int32 i = 0; i < CountRows(); i++) {
			PackageRow* row = dynamic_cast<PackageRow*>(RowAt(i));
			if (row == NULL || row->Package() == NULL)
				continue;
			if (!wanted.ContainsKey(row_key(row->Package())))
				removedRows.AddItem(row);
		}
		RemoveRows(&removedRows);
		for (int32 i = removedRows.CountItems() - 1; i >= 0; i--) {
			PackageRow* row = (PackageRow*)removedRows.ItemAt(i);
			fRowByKey.Remove(row_key(row->Package()));
			delete row;
		}
	}

	BList addedRows;
	// Walk the model, not the hash map, so rows are created in the
	// model's (sorted) order.
	for (int32 i = 0; i < packages.CountItems(); i++) {
		PackageInfo* package = packages.ItemAt(i);
		if (!wanted.ContainsKey(row_key(package)))
			continue;
		if (fRowByKey.ContainsKey(row_key(package)))
			continue;

		PackageRow* row = new PackageRow(package);
		addedRows.AddItem(row);
		fRowByKey.Put(row_key(package), row);
	}

	if (!addedRows.IsEmpty())
		AddRows(&addedRows, -1, NULL);
}


PackageRow*
PackageListView::_FindRow(const PackageInfo* package) const
{
	if (package == NULL)
		return NULL;

	return fRowByKey.Get(row_key(package));
}


void
PackageListView::ClearPackages()
{
	Inherited::Clear();
	fRowByKey.Clear();
}


PackageRow*
PackageListView::SelectedRow()
{
	return dynamic_cast<PackageRow*>(CurrentSelection());
}


PackageInfo*
PackageListView::SelectedPackage()
{
	PackageRow* row = SelectedRow();
	if (row == NULL)
		return NULL;

	return row->Package();
}


void
PackageListView::RefreshRow(PackageInfo* package)
{
	PackageRow* row = _FindRow(package);
	if (row == NULL)
		return;

	row->Refresh();
	UpdateRow(row);
}


int32
PackageListView::MarkSelected(package_mark mark)
{
	int32 count = 0;
	BRow* row = NULL;
	while ((row = CurrentSelection(row)) != NULL) {
		PackageRow* packageRow = dynamic_cast<PackageRow*>(row);
		if (packageRow == NULL || packageRow->Package() == NULL)
			continue;

		packageRow->SetMark(mark);
		packageRow->Refresh();
		UpdateRow(packageRow);
		count++;
	}
	return count;
}


void
PackageListView::_AddColumns()
{
	AddColumn(new MarkColumn("", 20, 20, 20, B_ALIGN_CENTER), kIconColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Package"), 220, 60, 600,
		B_TRUNCATE_END), kNameColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Version"), 120, 60, 300,
		B_TRUNCATE_END), kVersionColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Category"), 120, 60, 300,
		B_TRUNCATE_END), kSectionColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Channel"), 100, 60, 240,
		B_TRUNCATE_END), kChannelColumn);
	AddColumn(new BSizeColumn(B_TRANSLATE("Size"), 90, 60, 200,
		B_ALIGN_RIGHT), kSizeColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Status"), 130, 60, 300,
		B_TRUNCATE_END), kStatusColumn);

	SetSortColumn(ColumnAt(kNameColumn), false, true);
}


bool
PackageListView::_Matches(PackageInfo* package, const BString& filter,
	const BString& section, status_filter status) const
{
	if (package == NULL)
		return false;

	if (filter.Length() > 0
		&& package->Name().IFindFirst(filter) < 0
		&& package->Summary().IFindFirst(filter) < 0) {
		return false;
	}

	if (section.Length() > 0 && package->Category() != section)
		return false;

	switch (status) {
		case kFilterInstalled:
			return package->State() != kPackageAvailable;
		case kFilterUpgradable:
			return package->State() == kPackageUpgradable;
		case kFilterMarked:
			return package->Mark() != kMarkNone;
		default:
			return true;
	}
}
