/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "DownloadsWindow.h"
#include "MainWindow.h"
#include "NetPlusApp.h"
#include "NetPlusMessages.h"
#include "SettingsWindow.h"
#include "StatusBarView.h"
#include "ToolbarView.h"

#include <TextControl.h>
#include <WebDefs.h>
#include <WebView.h>
#include <WebBookmarks.h>
#include <WebSettings.h>
#include <UrlNormalize.h>

#include <Alert.h>
#include <Application.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Rect.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>


MainWindow::MainWindow(BRect frame, const char* url, bool privateWindow)
	:
	BWindow(frame, "NetPlus", B_TITLED_WINDOW, 0),
	fWebView(nullptr),
	fToolbar(nullptr),
	fStatusBar(nullptr),
	fMenuBar(nullptr),
	fBookmarksMenu(nullptr),
	fRemoveBookmarkMenu(nullptr),
	fBackItem(nullptr),
	fForwardItem(nullptr),
	fPrivateWindow(privateWindow),
	fFullScreen(false),
	fSavedFrame(frame)
{
	BRect viewRect(0, 0, frame.Width(), frame.Height());

	fMenuBar = new BMenuBar("menubar");

	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Open Location" B_UTF8_ELLIPSIS,
		new BMessage(MSG_OPEN_URL), 'L'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("New Window",
		new BMessage(MSG_OPEN_WINDOW), 'N'));
	BMenuItem* privateItem = new BMenuItem("New Private Window",
		new BMessage(MSG_OPEN_WINDOW), 'N', B_SHIFT_KEY);
	privateItem->Message()->AddBool("private", true);
	fileMenu->AddItem(privateItem);
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Settings" B_UTF8_ELLIPSIS,
		new BMessage(MSG_SHOW_SETTINGS)));
	fileMenu->AddItem(new BMenuItem("Show Downloads",
		new BMessage(MSG_SHOW_DOWNLOADS), 'J'));
	fileMenu->AddSeparatorItem();
	// Quit closes the whole application, like the legacy browser.
	BMenuItem* quitItem = new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q');
	quitItem->SetTarget(be_app);
	fileMenu->AddItem(quitItem);
	fMenuBar->AddItem(fileMenu);

	BMenu* editMenu = new BMenu("Edit");
	editMenu->AddItem(new BMenuItem("Cut",
		new BMessage(B_WEB_EDIT_CUT), 'X'));
	editMenu->AddItem(new BMenuItem("Copy",
		new BMessage(B_WEB_EDIT_COPY), 'C'));
	editMenu->AddItem(new BMenuItem("Paste",
		new BMessage(B_WEB_EDIT_PASTE), 'V'));
	editMenu->AddSeparatorItem();
	editMenu->AddItem(new BMenuItem("Select All",
		new BMessage(B_WEB_EDIT_SELECT_ALL), 'A'));
	fMenuBar->AddItem(editMenu);

	BMenu* goMenu = new BMenu("Go");
	fBackItem = new BMenuItem("Back",
		new BMessage(MSG_NAV_BACK), B_LEFT_ARROW);
	fForwardItem = new BMenuItem("Forward",
		new BMessage(MSG_NAV_FORWARD), B_RIGHT_ARROW);
	goMenu->AddItem(fBackItem);
	goMenu->AddItem(fForwardItem);
	goMenu->AddSeparatorItem();
	goMenu->AddItem(new BMenuItem("Reload",
		new BMessage(MSG_NAV_RELOAD), 'R'));
	goMenu->AddItem(new BMenuItem("Stop",
		new BMessage(MSG_NAV_STOP), 0));
	goMenu->AddItem(new BMenuItem("Home",
		new BMessage(MSG_NAV_HOME), 'H'));
	fMenuBar->AddItem(goMenu);

	fBookmarksMenu = new BMenu("Bookmarks");
	fBookmarksMenu->AddItem(new BMenuItem("Add Bookmark",
		new BMessage(MSG_BOOKMARK_ADD), 'D'));
	fRemoveBookmarkMenu = new BMenu("Remove Bookmark");
	fBookmarksMenu->AddItem(fRemoveBookmarkMenu);
	fBookmarksMenu->AddSeparatorItem();
	fMenuBar->AddItem(fBookmarksMenu);

	BMenu* helpMenu = new BMenu("Help");
	helpMenu->AddItem(new BMenuItem("About NetPlus",
		new BMessage(B_ABOUT_REQUESTED)));
	fMenuBar->AddItem(helpMenu);

	fToolbar   = new ToolbarView(viewRect);
	fWebView   = new BWebView(viewRect, "webview");
	fStatusBar = new StatusBarView(viewRect);

	fWebView->SetTarget(BMessenger(this));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(fToolbar)
		.Add(fWebView, 1.0f)
		.Add(fStatusBar);

	BWebSettings* settings = NetPlusApp::Settings();

	fWebView->SetSession(NULL,
		fPrivateWindow || settings->PrivateBrowsing());
	_ApplySettings();
	_RebuildBookmarksMenu();

	const char* startup = (url != NULL && url[0] != '\0')
		? url : settings->Homepage();
	fWebView->LoadURL(startup);
}


MainWindow::~MainWindow()
{
}


void
MainWindow::_ApplySettings()
{
	BWebSettings* settings = NetPlusApp::Settings();

	fWebView->SetJavaScriptEnabled(settings->JavaScriptEnabled());
	fWebView->SetImagesEnabled(settings->ImagesEnabled());

	const char* suffix = settings->UserAgentSuffix();
	fWebView->SetUserAgentSuffix(suffix != NULL ? suffix : "");

	const char* blocked = settings->BlockedDomains();
	fWebView->SetBlockedDomains(blocked != NULL ? blocked : "");

	fWebView->SetZoomLevel((float)settings->ZoomDefault());

	const char* dir = settings->DownloadDir();
	fWebView->SetDownloadDirectory(dir);
	fWebView->SetAskDownloadLocation(settings->AskDownloadLocation());

	// The start page search box follows the configured engine.
	BString script("window.NETPLUS_SEARCH_TEMPLATE = '");
	const char* templateURL = _SearchTemplate();
	for (const char* c = templateURL; *c != '\0'; c++) {
		if (*c == '\'' || *c == '\\')
			script << '\\';
		script << *c;
	}
	script << "';";
	fWebView->AddUserScript(script.String(), true);
}


const char*
MainWindow::_SearchTemplate() const
{
	BWebSettings* settings = NetPlusApp::Settings();
	const char* builtin
		= NetPlusApp::SearchTemplateFor(settings->SearchEngine());
	if (builtin != NULL)
		return builtin;
	return settings->SearchURLTemplate();
}


void
MainWindow::_OpenURL(const char* text)
{
	if (text == NULL || text[0] == '\0')
		return;

	std::string url = web_url_for_user_input(text, _SearchTemplate());
	if (url.empty())
		return;

	fWebView->LoadURL(url.c_str());
	if (fToolbar != NULL)
		fToolbar->SetLoading(true);
}


void
MainWindow::_UpdateNavState()
{
	if (fWebView == NULL)
		return;

	bool canBack = fWebView->CanGoBack();
	bool canForward = fWebView->CanGoForward();
	bool loading = fWebView->IsLoading();

	if (fToolbar != NULL) {
		fToolbar->SetCanGoBack(canBack);
		fToolbar->SetCanGoForward(canForward);
		fToolbar->SetLoading(loading);
	}
	if (fBackItem != NULL)
		fBackItem->SetEnabled(canBack);
	if (fForwardItem != NULL)
		fForwardItem->SetEnabled(canForward);
}


void
MainWindow::_RebuildBookmarksMenu()
{
	if (fBookmarksMenu == NULL || fRemoveBookmarkMenu == NULL)
		return;

	BWebBookmarks* bookmarks = NetPlusApp::Bookmarks();

	// Keep the Add item and the separator before the list.
	while (fBookmarksMenu->CountItems() > 3) {
		delete fBookmarksMenu->RemoveItem((int32)3);
	}
	while (fRemoveBookmarkMenu->CountItems() > 0) {
		delete fRemoveBookmarkMenu->RemoveItem((int32)0);
	}

	for (int32 i = 0; i < bookmarks->Count(); i++) {
		BMessage* goMessage = new BMessage(MSG_BOOKMARK_GO);
		goMessage->AddString("url", bookmarks->URLAt(i));
		fBookmarksMenu->AddItem(new BMenuItem(bookmarks->TitleAt(i),
			goMessage));

		BMessage* removeMessage = new BMessage(MSG_BOOKMARK_REMOVE);
		removeMessage->AddString("url", bookmarks->URLAt(i));
		fRemoveBookmarkMenu->AddItem(new BMenuItem(bookmarks->TitleAt(i),
			removeMessage));
	}

	fRemoveBookmarkMenu->SetEnabled(bookmarks->Count() > 0);
}


void
MainWindow::_SetFullScreen(bool enter)
{
	if (enter == fFullScreen)
		return;

	if (enter) {
		fSavedFrame = Frame();
		fMenuBar->Hide();
		fToolbar->Hide();
		fStatusBar->Hide();
		BRect screen = BScreen(this).Frame();
		MoveTo(screen.left, screen.top);
		ResizeTo(screen.Width(), screen.Height());
	} else {
		MoveTo(fSavedFrame.left, fSavedFrame.top);
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		fMenuBar->Show();
		fToolbar->Show();
		fStatusBar->Show();
	}
	fFullScreen = enter;
}


bool
MainWindow::QuitRequested()
{
	// The downloads window may hold our view for cancel; clear it
	// before the view dies with the window.
	DownloadsWindow::DetachWebView(fWebView);

	BWebSettings* settings = NetPlusApp::Settings();
	if (settings->RestoreSession() && fWebView != NULL
		&& !fPrivateWindow && !settings->PrivateBrowsing()) {
		settings->SetValue("last_session_url", fWebView->CurrentURL());
		settings->Save();
	}

	static_cast<NetPlusApp*>(be_app)->WindowClosed(this);
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
			if (fWebView) fWebView->Reload();
			break;

		case MSG_NAV_STOP:
			if (fWebView) fWebView->Stop();
			break;

		case MSG_NAV_HOME:
			if (fWebView) {
				fWebView->LoadURL(NetPlusApp::Settings()->Homepage());
				if (fToolbar) fToolbar->SetLoading(true);
			}
			break;

		case MSG_OPEN_URL:
		{
			// The URL bar sends no text with its message: read it
			// from the control. Other senders pass it as "url".
			const char* url = nullptr;
			BTextControl* source = nullptr;
			if (message->FindString("url", &url) != B_OK
				&& message->FindPointer("source", (void**)&source) == B_OK
				&& source != nullptr)
				url = source->Text();
			_OpenURL(url);
			break;
		}

		case MSG_OPEN_WINDOW:
		{
			const char* url = nullptr;
			bool priv = false;
			message->FindString("url", &url);
			message->FindBool("private", &priv);
			static_cast<NetPlusApp*>(be_app)->OpenWindow(url, priv);
			break;
		}

		case MSG_SETTINGS_CHANGED:
			// New windows pick the session policy up themselves;
			// the open view keeps its session but re-reads the
			// rest.
			_ApplySettings();
			break;

		case MSG_SHOW_DOWNLOADS:
			DownloadsWindow::ShowWindow(fWebView);
			break;

		case MSG_SHOW_SETTINGS:
			SettingsWindow::ShowWindow(Frame());
			break;

		case MSG_BOOKMARK_ADD:
		{
			BWebBookmarks* bookmarks = NetPlusApp::Bookmarks();
			const char* url = fWebView ? fWebView->CurrentURL() : NULL;
			if (url == NULL || url[0] == '\0')
				break;
			const char* title = fWebView->Title();
			if (title == NULL || title[0] == '\0')
				title = url;
			bookmarks->Add(title, url);
			bookmarks->Save();
			static_cast<NetPlusApp*>(be_app)->BroadcastBookmarksChanged();
			break;
		}

		case MSG_BOOKMARK_GO:
		{
			const char* url = nullptr;
			if (message->FindString("url", &url) == B_OK && fWebView)
				_OpenURL(url);
			break;
		}

		case MSG_BOOKMARK_REMOVE:
		{
			const char* url = nullptr;
			if (message->FindString("url", &url) == B_OK) {
				BWebBookmarks* bookmarks = NetPlusApp::Bookmarks();
				bookmarks->Remove(url);
				bookmarks->Save();
				static_cast<NetPlusApp*>(be_app)
					->BroadcastBookmarksChanged();
			}
			break;
		}

		case MSG_BOOKMARKS_CHANGED:
			_RebuildBookmarksMenu();
			break;

		case B_WEB_EDIT_CUT:
			if (fWebView) fWebView->Cut();
			break;

		case B_WEB_EDIT_COPY:
			if (fWebView) fWebView->Copy();
			break;

		case B_WEB_EDIT_PASTE:
			if (fWebView) fWebView->Paste();
			break;

		case B_WEB_EDIT_SELECT_ALL:
			if (fWebView) fWebView->SelectAll();
			break;

		case B_WEB_PAGE_TITLE_CHANGED:
		{
			const char* title = nullptr;
			if (message->FindString(B_WEB_TITLE_FIELD, &title) == B_OK
				&& title != nullptr && title[0] != '\0') {
				BString windowTitle("NetPlus - ");
				windowTitle << title;
				SetTitle(windowTitle.String());
			}
			break;
		}

		case B_WEB_PAGE_URL_CHANGED:
		{
			const char* url = nullptr;
			if (message->FindString(B_WEB_URL_FIELD, &url) == B_OK
				&& url != nullptr) {
				fStatusBase = url;
				if (fToolbar)    fToolbar->SetURL(url);
				if (fStatusBar)  fStatusBar->SetStatusText(url);
			}
			_UpdateNavState();
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
			_UpdateNavState();
			if (fStatusBar) fStatusBar->ClearProgress();
			break;

		case B_WEB_PAGE_LOAD_FAILED:
		{
			const char* err = nullptr;
			if (message->FindString(B_WEB_ERROR_FIELD, &err) == B_OK
				&& fStatusBar && err != nullptr)
				fStatusBar->SetStatusText(err);
			_UpdateNavState();
			break;
		}

		case B_WEB_PAGE_HOVER_CHANGED:
		{
			const char* link = nullptr;
			message->FindString(B_WEB_URL_FIELD, &link);
			if (fStatusBar != NULL) {
				if (link != nullptr && link[0] != '\0')
					fStatusBar->SetStatusText(link);
				else
					fStatusBar->SetStatusText(fStatusBase.String());
			}
			break;
		}

		case B_WEB_PAGE_PROCESS_CRASHED:
			if (fStatusBar != NULL)
				fStatusBar->SetStatusText(
					"The page crashed. Use Reload to try again.");
			_UpdateNavState();
			break;

		case B_WEB_PAGE_NEW_WINDOW:
		{
			const char* url = nullptr;
			if (message->FindString(B_WEB_URL_FIELD, &url) == B_OK)
				static_cast<NetPlusApp*>(be_app)->OpenWindow(url,
					false);
			break;
		}

		case B_WEB_PAGE_FULLSCREEN:
		{
			bool entered = false;
			message->FindBool(B_WEB_FULLSCREEN_FIELD, &entered);
			_SetFullScreen(entered);
			break;
		}

		case B_WEB_OPEN_LOCATION:
		{
			const char* url = nullptr;
			bool newWindow = false;
			message->FindString(B_WEB_URL_FIELD, &url);
			message->FindBool("new_window", &newWindow);
			if (url == nullptr || url[0] == '\0')
				break;
			if (newWindow)
				static_cast<NetPlusApp*>(be_app)->OpenWindow(url,
					false);
			else
				_OpenURL(url);
			break;
		}

		case B_WEB_DOWNLOAD_STARTED:
		case B_WEB_DOWNLOAD_PROGRESS:
		case B_WEB_DOWNLOAD_FINISHED:
		case B_WEB_DOWNLOAD_FAILED:
			DownloadsWindow::HandleDownloadMessage(message, fWebView);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
