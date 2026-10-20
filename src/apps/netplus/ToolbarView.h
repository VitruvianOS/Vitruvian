/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef TOOLBAR_VIEW_H
#define TOOLBAR_VIEW_H


#include <ToolBar.h>
#include <View.h>


class BBitmap;
class BButton;
class BMessage;
class HistoryStore;
class UrlBarView;


/*!	Navigation strip rebuilt on the public BToolBar: back, forward,
	reload and home are BToolBar icon actions, so the bar's built-in
	overflow menu collects them when the window narrows. The URL bar
	is an UrlBarView embedded with AddView: it joins the strip and
	the overflow sizing without ever collapsing into the overflow
	menu itself. Downloads and the app-menu (hamburger) buttons are
	BToolBar actions at the right end.
*/
class ToolbarView : public BView {
public:
								ToolbarView(BRect frame,
									HistoryStore* history);
	virtual						~ToolbarView();

	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

			void				SetURL(const char* url);
			void				SetLoading(bool loading);
			void				SetCanGoBack(bool can);
			void				SetCanGoForward(bool can);
			void				SetDownloadsActive(int32 count);
			void				SetSearchEngine(int32 engine);

			void				FocusURL();
			bool				DownloadsAnchor(BPoint* anchor) const;

private:
			BToolBar*			fToolBar;
			UrlBarView*			fURLBar;
			BBitmap*			fReloadIcon;
			BBitmap*			fStopIcon;
			BBitmap*			fDownloadIcon;
			BBitmap*			fDownloadActiveIcon;
			bool				fLoading;
};


#endif // TOOLBAR_VIEW_H
