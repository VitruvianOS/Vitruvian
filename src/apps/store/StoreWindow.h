/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _STORE_WINDOW_H
#define _STORE_WINDOW_H

#include <ObjectList.h>
#include <String.h>
#include <Window.h>

#include <package/PackageDefs.h>

#include "StoreCatalog.h"

class BWebView;
class PackageWorker;
class VPackageRoster;


class StoreWindow : public BWindow {
public:
								StoreWindow(BRect frame, bool dryRun);
	virtual						~StoreWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_LoadPage();
			void				_LoadCatalog();
			void				_FetchCatalog();
	static	status_t			_FetchThread(void* data);
			void				_DispatchPageAction(const char* payload);
			void				_ReloadStates();
			void				_PushState();
			void				_StartTransaction(bool install,
									StoreEntry* entry);
			void				_SetStatus(const BString& status);
			void				_FinishBusy();
			void				_RunScript(const char* script);

			BString				_BuildStateJSON();
			BString				_BuildCategoriesJSON();
			BString				_EntryIconDataURI(StoreEntry* entry);
			bool				_IsInstalled(StoreEntry* entry) const;

			BWebView*			fWebView;
			PackageWorker*		fWorker;
			VPackageRoster*		fRoster;
			StoreCatalog		fCatalog;
			// Selected section kind: "app", "component" or "data".
			BString				fSection;
			BString				fSelectedCategory;
			// Debian package names reported installed by the roster.
			BObjectList<BString, true>	fInstalledPackages;
			int32				fPendingStateQueries;
			// In-flight install/remove, if any.
			BString				fBusyEntryId;
			int32				fBusyPercent;
			BString				fBusyStatus;
			bool				fDryRun;
};


#endif // _STORE_WINDOW_H
