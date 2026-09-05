/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_LIST_VIEW_H
#define PACKAGE_LIST_VIEW_H

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <HashMap.h>
#include <HashString.h>
#include <ObjectList.h>

#include "PackageManagerDefs.h"


class BPopUpMenu;
class PackageInfo;
class PackageRow;


class PackageListView : public BColumnListView {
	typedef BColumnListView Inherited;
public:
								PackageListView();
	virtual						~PackageListView();

	virtual	void				AttachedToWindow();
	virtual	void				MouseDown(BPoint where);

			void				SetPackages(
									const BObjectList<PackageInfo, true>& packages,
									const BString& filter,
									const BString& section,
									status_filter status);

			// Rows borrow their PackageInfo; call before freeing the model.
			void				ClearPackages();

			PackageRow*			SelectedRow();
			PackageInfo*		SelectedPackage();
			void				RefreshRow(PackageInfo* package);

			int32				MarkSelected(package_mark mark);

			// Gates the icon column's click-to-mark toggle; mirrors
			// MainWindow's transaction-active state.
			void				SetTransactionActive(bool active)
									{ fTransactionActive = active; }
			bool				TransactionActive() const
									{ return fTransactionActive; }

private:
			void				_AddColumns();
			bool				_Matches(PackageInfo* package,
									const BString& filter,
									const BString& section,
									status_filter status) const;

			PackageRow*			_FindRow(const PackageInfo* package) const;

			BPopUpMenu*			fContextMenu;
			HashMap<HashString, PackageRow*> fRowByKey;
			bool				fTransactionActive;
};


#endif // PACKAGE_LIST_VIEW_H
