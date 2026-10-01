/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "MainWindow.h"
#include "NetPlusMessages.h"
#include "StatusBarView.h"
#include "ToolbarView.h"

#include <WebDefs.h>
#include <WebView.h>

#include <Alert.h>
#include <Application.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Rect.h>
#include <View.h>


MainWindow::MainWindow(BRect frame, const char* startupURL)
	:
	BWindow(frame, "NetPlus", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
	fWebView(nullptr),
	fToolbar(nullptr),
	fStatusBar(nullptr)
{
	BRect viewRect(0, 0, frame.Width(), frame.Height());

	BMenuBar* menuBar = new BMenuBar("menubar");

	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Open Location" B_UTF8_ELLIPSIS,
		new BMessage(MSG_OPEN_URL), 'L'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(fileMenu);

	BMenu* goMenu = new BMenu("Go");
	goMenu->AddItem(new BMenuItem("Back",
		new BMessage(MSG_NAV_BACK), B_LEFT_ARROW));
	goMenu->AddItem(new BMenuItem("Forward",
		new BMessage(MSG_NAV_FORWARD), B_RIGHT_ARROW));
	goMenu->AddSeparatorItem();
	goMenu->AddItem(new BMenuItem("Reload",
		new BMessage(MSG_NAV_RELOAD), 'R'));
	goMenu->AddItem(new BMenuItem("Stop",
		new BMessage(MSG_NAV_STOP), 0));
	menuBar->AddItem(goMenu);

	BMenu* helpMenu = new BMenu("Help");
	helpMenu->AddItem(new BMenuItem("About NetPlus",
		new BMessage(B_ABOUT_REQUESTED)));
	menuBar->AddItem(helpMenu);

	fToolbar   = new ToolbarView(viewRect);
	fWebView   = new BWebView(viewRect, "webview");
	fStatusBar = new StatusBarView(viewRect);

	fWebView->SetTarget(BMessenger(this));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fToolbar)
		.Add(fWebView, 1.0f)
		.Add(fStatusBar);

	_ConnectNotifications();

	const char* url = (startupURL && startupURL[0]) ? startupURL : "about:blank";
	fWebView->LoadURL(url);
}


MainWindow::~MainWindow()
{
}


void
MainWindow::_ConnectNotifications()
{
}


bool
MainWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_ABOUT_REQUESTED:
		{
			BAlert* alert = new BAlert("About NetPlus",
				"NetPlus Browser v0.1\n"
				"WPE WebKit + Vitruvian\n"
				"\n\xa9 2026 VitruvianOS Project",
				"OK");
			alert->Go(nullptr);
			break;
		}

		case MSG_NAV_BACK:
			if (fWebView) fWebView->GoBack();
			break;

		case MSG_NAV_FORWARD:
			if (fWebView) fWebView->GoForward();
			break;

		case MSG_NAV_RELOAD:
			if (fWebView) {
				fWebView->Reload();
				if (fToolbar) fToolbar->SetLoading(true);
			}
			break;

		case MSG_NAV_STOP:
			if (fWebView) {
				fWebView->Stop();
				if (fToolbar) fToolbar->SetLoading(false);
			}
			break;

		case MSG_OPEN_URL:
		{
			const char* url = nullptr;
			if (message->FindString("_value", &url) != B_OK
				&& message->FindString("url", &url) != B_OK)
				break;
			if (fWebView && url) {
				fWebView->LoadURL(url);
				if (fToolbar) fToolbar->SetLoading(true);
			}
			break;
		}

		case B_WEB_PAGE_TITLE_CHANGED:
		{
			const char* title = nullptr;
			if (message->FindString(B_WEB_TITLE_FIELD, &title) == B_OK
				&& title) {
				BString windowTitle("NetPlus - ");
				windowTitle << title;
				SetTitle(windowTitle.String());
			}
			break;
		}

		case B_WEB_PAGE_URL_CHANGED:
		{
			const char* url = nullptr;
			if (message->FindString(B_WEB_URL_FIELD, &url) == B_OK && url) {
				if (fToolbar)    fToolbar->SetURL(url);
				if (fStatusBar)  fStatusBar->SetStatusText(url);
			}
			break;
		}

		case B_WEB_PAGE_PROGRESS_CHANGED:
		{
			double p = 0;
			if (message->FindDouble(B_WEB_PROGRESS_FIELD, &p) == B_OK)
				if (fStatusBar) fStatusBar->SetProgress(p);
			break;
		}

		case B_WEB_PAGE_LOAD_FINISHED:
			if (fToolbar) {
				fToolbar->SetLoading(false);
				if (fWebView) {
					// TODO: expose CanGoBack/CanGoForward via BWebView public API in v2
				}
			}
			if (fStatusBar) fStatusBar->ClearProgress();
			break;

		case B_WEB_PAGE_LOAD_FAILED:
		{
			const char* err = nullptr;
			if (fToolbar) fToolbar->SetLoading(false);
			if (fStatusBar && message->FindString(B_WEB_ERROR_FIELD, &err) == B_OK)
				fStatusBar->SetStatusText(err);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
