/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_INFO_VIEW_H
#define PACKAGE_INFO_VIEW_H

#include <GroupView.h>

#include "PackageManagerDefs.h"
#include "TruncatingStringView.h"


class BButton;
class BMessage;
class BOutlineListView;
class BTabView;
class BTextView;
class PackageInfo;


class PackageInfoView : public BGroupView {
	typedef BGroupView Inherited;
public:
								PackageInfoView();
	virtual						~PackageInfoView();

	virtual	void				AttachedToWindow();

			void				SetPackage(PackageInfo* package);
			void				SetDetails(const BMessage* details);
			void				SetChangelog(const BMessage* changelog);
			void				Clear();

			// Gates the select button; mirrors MainWindow's
			// transaction-active state.
			void				SetTransactionActive(bool active);

			// Re-derives the select button's label, target message and
			// enabled state from the package currently shown. The mark
			// can change without a reselection (glyph column, Package
			// menu, Mark all upgrades), so callers other than
			// SetPackage()/Clear() must call this to keep the button
			// from going stale; MainWindow::_UpdatePendingUI() does.
			void				RefreshSelectButton();

			// "Apply N changes"; disabled when count == 0 or the caller
			// says so (e.g. a transaction is already running).
			void				SetApplyState(int32 pendingCount,
									bool enabled);

private:
			void				_UpdateSelectButton();

			TruncatingStringView* fTitleView;
			TruncatingStringView* fVersionView;
			TruncatingStringView* fChannelView;
			BButton*			fSelectButton;
			BButton*			fApplyButton;
			BTabView*			fTabView;
			BTextView*			fDescriptionView;
			BOutlineListView*	fContentsView;
			BTextView*			fChangelogView;
			PackageInfo*		fPackage;
			bool				fTransactionActive;
};


#endif // PACKAGE_INFO_VIEW_H
