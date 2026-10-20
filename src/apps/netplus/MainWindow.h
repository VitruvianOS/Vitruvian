/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "DownloadsModel.h"
#include "HistoryStore.h"
#include "InstalledAppStore.h"
#include "WebAppManifest.h"


class BMenuItem;
class BMenu;
class BMenuBar;
class BWebView;
class DownloadsPanel;
class PanelMenu;
class StatusBarView;
class ToolbarView;


/*!	Handed to the window when it comes up as an installed PWA
	instead of as the browser.
*/
struct PwaLaunchInfo {
	bool		installed = false;
	std::string	id;
	std::string	name;
	std::string	startUrl;
	std::string	display;
	std::string	iconPath;
};


class MainWindow : public BWindow {
public:
								MainWindow(BRect frame, const char* url,
									bool privateWindow,
									const PwaLaunchInfo* pwa = NULL);
	virtual						~MainWindow();

	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);
	virtual	void				FrameMoved(BPoint where);

private:
			void				_ApplySettings();
			void				_UpdateNavState();
			void				_RebuildBookmarksMenu();
			void				_SetFullScreen(bool enter);
			void				_OpenURL(const char* text);
			const char*			_SearchTemplate() const;
			void				_AddHistory(const char* url);
			void				_ShowAppMenu(BPoint anchor);
			void				_ShowDownloads(BPoint anchor);
			void				_HandleDownloadMessage(BMessage* message);
			void				_RefreshDownloadUI();
			void				_HandleManifestMessage(BMessage* message);
			void				_HandleIconMessage(BMessage* message);
			void				_StartInstall();
			void				_FinishInstall(const std::string& iconPath);
			InstalledAppStore&	_Store();

			BWebView*			fWebView;
			ToolbarView*		fToolbar;
			StatusBarView*		fStatusBar;
			BMenuBar*			fMenuBar;
			BMenu*				fBookmarksMenu;
			BMenu*				fRemoveBookmarkMenu;
			BMenuItem*			fBackItem;
			BMenuItem*			fForwardItem;
			bool				fPrivateWindow;
			bool				fFullScreen;
			BRect				fSavedFrame;
			window_look			fSavedLook;
			float				fSavedLimits[4];
			BString				fStatusBase;
			PanelMenu*			fAppMenu;
			DownloadsPanel*		fDownloadsPanel;
			BPoint				fLastOrigin;
			BMessenger			fAppMenuWindow;
			BMessenger			fDownloadsPanelWindow;
			DownloadsModel		fDownloadsModel;
			HistoryStore		fHistory;
			int32				fSearchEngine;

			bool				fPwaMode;
			ManifestDisplayMode	fPwaDisplay;
			PwaLaunchInfo		fPwa;
			WebAppManifest		fManifest;
			std::string			fManifestUrl;
			std::string			fManifestPage;
			InstalledAppStore*	fStore;
};


#endif // MAIN_WINDOW_H
