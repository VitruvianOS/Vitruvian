/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_ROW_H
#define PACKAGE_ROW_H

#include <ColumnListView.h>

#include "PackageManagerDefs.h"


class PackageInfo;


enum {
	kIconColumn			= 0,
	kNameColumn			= 1,
	kVersionColumn		= 2,
	kSectionColumn		= 3,
	kChannelColumn		= 4,
	kSizeColumn			= 5,
	kStatusColumn		= 6,

	kColumnCount		= 7
};


class PackageRow : public BRow {
	typedef BRow Inherited;
public:
								PackageRow(PackageInfo* package);
	virtual						~PackageRow();

			// Borrowed; the window owns the model.
			PackageInfo*		Package() const
									{ return fPackage; }

			package_mark		Mark() const;
			void				SetMark(package_mark mark);

			void				Refresh();

private:
			PackageInfo*		fPackage;
};


#endif // PACKAGE_ROW_H
