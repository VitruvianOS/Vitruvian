/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_LIST_H
#define SERVICES_LIST_H

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <HashMap.h>
#include <HashString.h>
#include <ObjectList.h>

#include "ServicesDefs.h"
#include "ServicesUnit.h"


class ServicesRow : public BRow {
public:
								ServicesRow(const ServicesUnit& unit);

			ServicesUnit		fUnit;
};


class ServicesList : public BColumnListView {
	typedef BColumnListView Inherited;
public:
								ServicesList();
	virtual						~ServicesList();

	virtual	void				AttachedToWindow();

			// Replaces the visible row set. The view copies the units.
			void				SetUnits(
									BObjectList<ServicesUnit, true>& units,
									const BString& filter,
									unit_filter unitFilter);

			// Adds or updates one row; removes it when the filter rejects
			// the unit. Safe to call while the list is still loading.
			void				UpdateUnit(const ServicesUnit& unit);
			void				RemoveUnit(const BString& name);

			ServicesUnit		SelectedUnit() const;
			bool				HasSelection() const;

			void				ClearUnits();

private:
			void				_AddColumns();
			ServicesRow*		_FindRow(const BString& name) const;
			bool				_Matches(const ServicesUnit& unit) const;

			bool				fSortingEnabled;
			BString				fFilterText;
			unit_filter			fUnitFilter;
			HashMap<HashString, ServicesRow*>	fRowByKey;
			// Owning: rows and the filter model share unit copies.
			BObjectList<ServicesUnit, true>	fSource;
};


#endif // SERVICES_LIST_H
