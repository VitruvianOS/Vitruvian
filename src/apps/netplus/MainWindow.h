/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <String.h>
#include <Window.h>

#include "DownloadsModel.h"
#include "HistoryStore.h"


class BMenuItem;
class BMenu;
class BMenuBar;
class BWebView;
class DownloadsPanel;
class PanelMenu;
class StatusBarView;
class ToolbarView;


class MainWindow : public BWindow {
public:
								MainWindow(BRect frame, const char* url,
									bool privateWindow);
	virtual						~MainWindow();

	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

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
			BString				fStatusBase;
			PanelMenu*			fAppMenu;
			DownloadsPanel*		fDownloadsPanel;
			DownloadsModel		fDownloadsModel;
			HistoryStore		fHistory;
			int32				fSearchEngine;
};


#endif // MAIN_WINDOW_H
