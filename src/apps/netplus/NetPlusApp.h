/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef NET_PLUS_APP_H
#define NET_PLUS_APP_H


#include <Application.h>
#include <String.h>

class BWebBookmarks;
class BWebSettings;
class MainWindow;


// Application-wide NetPlus state: the profile settings, the open
// windows and the helpers both the main window and the settings and
// downloads windows share.
class NetPlusApp : public BApplication {
public:
								NetPlusApp();
	virtual						~NetPlusApp();

	virtual void				ArgvReceived(int32 argc, char** argv);
	virtual void				ReadyToRun();
	virtual void				MessageReceived(BMessage* message);

			void				OpenWindow(const char* url,
									bool privateWindow);

			// Called by MainWindow when it closes; the app quits
			// when the last window is gone.
			void				WindowClosed(MainWindow* window);

			void				BroadcastSettingsChanged();
			void				BroadcastBookmarksChanged();

	static	BWebSettings*		Settings();
	static	BWebBookmarks*		Bookmarks();
	// Directory holding settings, bookmarks and the profile.
	static	const char*			SettingsDir();
	// DuckDuckGo, Startpage, ... or "Custom": resolves to the
	// template with %s; "Custom" uses the stored custom template.
	static	const char*			SearchTemplateFor(const char* engine);

private:
			void				_CreateDirs();
			void				_LoadSettings();
			void				_LoadBookmarks();

	BWebSettings*				fSettings;
	BWebBookmarks*				fBookmarks;
			int32				fWindowCount;
			BString				fStartupURL;
};


#endif // NET_PLUS_APP_H
