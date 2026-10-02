/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ServicesList.h"

#include <Catalog.h>
#include <Font.h>
#include <Locale.h>
#include <Message.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ServicesList"


enum {
	kNameColumn		= 0,
	kDescriptionColumn,
	kActiveColumn,
	kSubColumn,
	kEnabledColumn,

	kColumnCount
};


// Keep the column maxima well below a full-screen width so the list
// cannot force the window wider when a long description shows up.
static const float kNameMaxWidth	= 280;
static const float kDescMaxWidth	= 360;
static const float kStateMaxWidth	= 120;


ServicesRow::ServicesRow(const ServicesUnit& unit)
	:
	fUnit(unit)
{
	SetField(new BStringField(unit.fName.String()), kNameColumn);
	SetField(new BStringField(unit.fDescription.String()),
		kDescriptionColumn);
	SetField(new BStringField(unit.fActiveState.String()), kActiveColumn);
	SetField(new BStringField(unit.fSubState.String()), kSubColumn);
	SetField(new BStringField(unit.fUnitFileState.String()),
		kEnabledColumn);
}


ServicesList::ServicesList()
	:
	Inherited("services", B_NAVIGABLE, B_FANCY_BORDER, true),
	fSortingEnabled(false),
	fUnitFilter(kFilterServicesOnly)
{
	// Must precede _AddColumns(): SetSortColumn() is a no-op while
	// sorting is disabled.
	SetSortingEnabled(true);
	fSortingEnabled = true;
	_AddColumns();
	SetSelectionMode(B_SINGLE_SELECTION_LIST);
}


ServicesList::~ServicesList()
{
	ClearUnits();
}


void
ServicesList::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	SetSelectionMessage(new BMessage(kMsgUnitSelected));
	SetTarget(Window());
}


void
ServicesList::_AddColumns()
{
	float nameWidth = be_plain_font->StringWidth("bluetooth.service") + 20;
	if (nameWidth < 140)
		nameWidth = 140;

	AddColumn(new BStringColumn(B_TRANSLATE("Unit"), nameWidth, 80,
		kNameMaxWidth, B_TRUNCATE_END), kNameColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Description"), 200, 80,
		kDescMaxWidth, B_TRUNCATE_END), kDescriptionColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Active"), 80, 50,
		kStateMaxWidth, B_TRUNCATE_END), kActiveColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Sub"), 70, 40,
		kStateMaxWidth, B_TRUNCATE_END), kSubColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Enabled"), 80, 50,
		kStateMaxWidth, B_TRUNCATE_END), kEnabledColumn);

	SetSortColumn(ColumnAt(kNameColumn), false, true);
}


bool
ServicesList::_Matches(const ServicesUnit& unit) const
{
	return unit.MatchesFilter(fUnitFilter)
		&& unit.MatchesText(fFilterText);
}


void
ServicesList::ClearUnits()
{
	Inherited::Clear();
	fSource.MakeEmpty();
	fRowByKey.Clear();
}


void
ServicesList::SetUnits(BObjectList<ServicesUnit, true>& units,
	const BString& filter, unit_filter unitFilter)
{
	fFilterText = filter;
	fUnitFilter = unitFilter;
	ClearUnits();

	BList rows;
	for (int32 i = 0; i < units.CountItems(); i++) {
		const ServicesUnit* source = units.ItemAt(i);
		if (source == NULL)
			continue;
		ServicesUnit unit = *source;
		if (!_Matches(unit))
			continue;

		fSource.AddItem(new ServicesUnit(unit));
		ServicesRow* row = new ServicesRow(unit);
		fRowByKey.Put(HashString(unit.fName.String()), row);
		rows.AddItem(row);
	}

	if (!rows.IsEmpty())
		AddRows(&rows, -1, NULL);
}


void
ServicesList::UpdateUnit(const ServicesUnit& unit)
{
	// Keep the source copy current so later filter passes see it.
	ServicesUnit* existing = NULL;
	for (int32 i = 0; i < fSource.CountItems(); i++) {
		ServicesUnit* item = fSource.ItemAt(i);
		if (item != NULL && item->fName == unit.fName) {
			existing = item;
			break;
		}
	}
	if (existing != NULL)
		*existing = unit;

	ServicesRow* row = _FindRow(unit.fName);
	if (!_Matches(unit)) {
		if (row != NULL) {
			RemoveRow(row);
			fRowByKey.Remove(HashString(unit.fName.String()));
			delete row;
		}
		return;
	}

	if (row == NULL) {
		fSource.AddItem(new ServicesUnit(unit));
		row = new ServicesRow(unit);
		fRowByKey.Put(HashString(unit.fName.String()), row);
		AddRow(row);
		return;
	}

	row->fUnit = unit;
	row->SetField(new BStringField(unit.fName.String()), kNameColumn);
	row->SetField(new BStringField(unit.fDescription.String()),
		kDescriptionColumn);
	row->SetField(new BStringField(unit.fActiveState.String()),
		kActiveColumn);
	row->SetField(new BStringField(unit.fSubState.String()), kSubColumn);
	row->SetField(new BStringField(unit.fUnitFileState.String()),
		kEnabledColumn);
	UpdateRow(row);
}


void
ServicesList::RemoveUnit(const BString& name)
{
	ServicesRow* row = _FindRow(name);
	if (row != NULL) {
		RemoveRow(row);
		fRowByKey.Remove(HashString(name.String()));
		delete row;
	}

	for (int32 i = 0; i < fSource.CountItems(); i++) {
		ServicesUnit* existing = fSource.ItemAt(i);
		if (existing != NULL && existing->fName == name) {
			fSource.RemoveItem(existing);
			break;
		}
	}
}


ServicesUnit
ServicesList::SelectedUnit() const
{
	ServicesUnit empty;
	ServicesRow* row = dynamic_cast<ServicesRow*>(CurrentSelection());
	if (row == NULL)
		return empty;
	return row->fUnit;
}


bool
ServicesList::HasSelection() const
{
	return CurrentSelection() != NULL;
}


ServicesRow*
ServicesList::_FindRow(const BString& name) const
{
	return fRowByKey.Get(HashString(name.String()));
}
