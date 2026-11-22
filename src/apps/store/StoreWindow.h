/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _STORE_WINDOW_H
#define _STORE_WINDOW_H

#include <ObjectList.h>
#include <String.h>
#include <Window.h>

#include <set>
#include <string>

#include <package/PackageDefs.h>

#include "StoreCatalog.h"

class BWebView;
class PackageWorker;
class IconFetcher;


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

			BString				_BuildStateJSON(BMessage& missingIcons);
			BString				_BuildCategoriesJSON();
			void				_AppendEntryJSON(BString& json,
									StoreEntry* entry,
									BMessage& missingIcons);
			BString				_EntryIconURL(StoreEntry* entry,
									BMessage& missingIcons);
			bool				_Matches(StoreEntry* entry) const;
			bool				_IsInstalled(StoreEntry* entry) const;

			BWebView*			fWebView;
			PackageWorker*		fWorker;
			IconFetcher*		fFetcher;
			StoreCatalog		fCatalog;
			// Selected section kind: "app", "component" or "data".
			BString				fSection;
			BString				fSelectedCategory;
			BString				fInfoEntryId;
			BMessage			fPendingApply;
			// Lower-case search text and how many entries of the
			// filtered list the page has asked for.
			BString				fQuery;
			int32				fLimit;
			// Debian package names installed, from the dpkg status file.
			std::set<std::string> fInstalled;
			int32				fStateSeq;
			BString				fLastIconRequest;
			std::set<std::string> fVisible;
			// In-flight install/remove, if any.
			BString				fBusyEntryId;
			int32				fBusyPercent;
			BString				fBusyStatus;
			int32				fBusyCancel;
			BString				fLastState;
			bool				fDryRun;
};


#endif // _STORE_WINDOW_H
