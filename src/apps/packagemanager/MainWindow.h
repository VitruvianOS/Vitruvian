/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <ObjectList.h>
#include <String.h>
#include <Window.h>

#include "PackageManagerDefs.h"
#include "TruncatingStringView.h"


class BMenuBar;
class BMenuItem;
class BMenu;
class BTabView;
class AptLogView;
class FilterView;
class PackageInfo;
class PackageInfoView;
class PackageListView;
class PackageWorker;
class WorkStatusView;


class MainWindow : public BWindow {
	typedef BWindow Inherited;
public:
								MainWindow(BRect frame);
	virtual						~MainWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();

private:
			void				_BuildMenuBar();
			void				_BuildLayout();

			void				_ApplyFilter();
			int32				_CountMarked() const;
			void				_UpdatePendingUI();
			void				_HandleSimulateReady(BMessage* message);
			void				_MarkSelected(package_mark mark);
			void				_ClearMarks(BMessage* message);
			void				_ApplyChanges();
			void				_SetTransactionActive(bool active);
			void				_HandleListReady(BMessage* message);
			void				_HandleError(BMessage* message);
			bool				_RunExitGuard();

			void				_WatchDpkgStatus();
			void				_ShowStaleHint(bool show);

		BMenuBar*			fMenuBar;
		BMenuItem*			fApplyItem;
		BMenuItem*			fRefreshItem;
		BMenuItem*			fMarkAllUpgradesItem;
		BMenuItem*			fCancelItem;
		BMenu*				fClearMarksMenu;
		BMenuItem*			fClearMarksItem;
			TruncatingStringView* fStaleHintView;
			FilterView*			fFilterView;
			BTabView*			fMainTabView;
			PackageListView*	fListView;
			PackageInfoView*	fInfoView;
			AptLogView*			fLogView;
			WorkStatusView*		fStatusView;
			PackageWorker*		fWorker;

			BObjectList<PackageInfo, true>	fPackages;
			bool				fTransactionActive;
};


#endif // MAIN_WINDOW_H
