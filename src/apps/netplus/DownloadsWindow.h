/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef DOWNLOADS_WINDOW_H
#define DOWNLOADS_WINDOW_H


#include <Window.h>

#include <map>
#include <stdint.h>


class BButton;
class BListView;
class BWebView;
class DownloadItem;


// Shows the profile's downloads with progress, cancel, open and
// show-in-Tracker. One window per application; the main windows
// forward their B_WEB_DOWNLOAD_* messages here.
class DownloadsWindow : public BWindow {
public:
								DownloadsWindow(BRect frame,
									BWebView* webView);
	virtual						~DownloadsWindow();

	virtual void				MessageReceived(BMessage* message);

	// Creates the window on first use and shows it.
	static	void				ShowWindow(BWebView* webView);
	// Clears a view pointer that is about to die with its window.
	static	void				DetachWebView(BWebView* webView);
	static	void				HandleDownloadMessage(BMessage* message,
									BWebView* webView);

private:
			void				_UpsertRow(uint64 id, const char* filename,
									const char* destination,
									uint64 received, uint64 total,
									bool finished, bool failed);
			DownloadItem*		_ItemForID(uint64 id) const;
			DownloadItem*		_Selected() const;
			void				_UpdateButtons();
			void				_UpdateLabel(DownloadItem* item);

			BListView*			fList;
			BButton*			fCancelButton;
			BButton*			fOpenButton;
			BButton*			fShowButton;
			BWebView*			fWebView;

	static	DownloadsWindow*	sInstance;
};


#endif // DOWNLOADS_WINDOW_H
