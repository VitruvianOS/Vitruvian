/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FILTER_VIEW_H
#define FILTER_VIEW_H

#include <GroupView.h>
#include <String.h>

#include "PackageManagerDefs.h"


class BMenu;
class BMenuField;
class BTextControl;
#include <ObjectList.h>


class FilterView : public BGroupView {
	typedef BGroupView Inherited;
public:
								FilterView();
	virtual						~FilterView();

	virtual	void				AttachedToWindow();

			BString				SearchText() const;
			BString				Section() const
									{ return fSection; }
			status_filter		Status() const
									{ return fStatus; }

			void				SetSections(
									const BObjectList<BString, true>& sections);
			void				SetSection(const char* section);
			void				SetStatus(status_filter status);

private:
			BTextControl*		fSearchControl;
			BMenu*				fSectionMenu;
			BMenuField*			fSectionField;
			BMenu*				fStatusMenu;
			BMenuField*			fStatusField;
			BString				fSection;
			status_filter		fStatus;
};


#endif // FILTER_VIEW_H
