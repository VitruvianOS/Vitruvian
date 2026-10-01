/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <String.h>
#include <Window.h>

class BWebView;
class StatusBarView;
class ToolbarView;


class MainWindow : public BWindow {
public:
								MainWindow(BRect frame, const char* startupURL);
	virtual						~MainWindow();

	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

private:
			void				_ConnectNotifications();

			BWebView*			fWebView;
			ToolbarView*		fToolbar;
			StatusBarView*		fStatusBar;
};


#endif // MAIN_WINDOW_H
