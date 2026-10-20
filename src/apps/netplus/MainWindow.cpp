/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "DownloadsWindow.h"
#include "DeskbarEntry.h"
#include "MainWindow.h"
#include "NetPlusApp.h"
#include "NetPlusMessages.h"
#include "SettingsWindow.h"
#include "PanelMenu.h"
#include "PanelMenuModel.h"
#include "StatusBarView.h"
#include "ToolbarView.h"
#include "UrlInput.h"

#include <WebDefs.h>
#include <WebView.h>
#include <WebBookmarks.h>
#include <WebSettings.h>
#include <UrlNormalize.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Entry.h>
#include <Invoker.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <Rect.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Built-in start page until the homepage becomes a setting.
static const char* kHomePage = "vos-about:";

// The handler name the install script posts to.
static const char* kInstallHandler = "netplus";

// Discovers <link rel="manifest"> after each document loads, fetches
// the manifest and posts it back; also exposes the icon fetch used
// at install time. Engine-side fetch keeps cookies and the proxy.
static const char* kInstallScript = (
	"(function() {"
	" var post = function(obj) {"
	"  try {"
	"   window.webkit.messageHandlers.netplus.postMessage("
	"    JSON.stringify(obj));"
	"  } catch (e) {}"
	" };"
	" var report = function() {"
	"  var link = document.querySelector('link[rel=\"manifest\"]');"
	"  if (link == null || !link.href)"
	"   return;"
	"  fetch(link.href, { credentials: 'include' })"
	"   .then(function(r) { return r.text(); })"
	"   .then(function(t) {"
	"    post({ handler: 'netplus-manifest', url: link.href,"
	"     page: location.href, text: t });"
	"   }).catch(function() {});"
	" };"
	" window.__netplusFetchIcon = function(url) {"
	"  fetch(url, { credentials: 'include' })"
	"   .then(function(r) { return r.arrayBuffer(); })"
	"   .then(function(b) {"
	"    var bytes = new Uint8Array(b);"
	"    var s = '';"
	"    for (var i = 0; i < bytes.length; i++)"
	"     s += String.fromCharCode(bytes[i]);"
	"    post({ handler: 'netplus-icon', url: url, data: btoa(s) });"
	"   }).catch(function() {"
	"    post({ handler: 'netplus-icon', url: url, data: '' });"
	"   });"
	"  return true;"
	" };"
	" if (document.readyState === 'loading')"
	"  document.addEventListener('DOMContentLoaded', report);"
	" else"
	"  report();"
	"})();"
);


static std::string
_SelfPath()
{
	char buffer[1024];
	ssize_t length = readlink("/proc/self/exe", buffer,
		sizeof(buffer) - 1);
	if (length <= 0)
		return std::string();
	buffer[length] = '\0';
	return std::string(buffer);
}


static BRect
_PwaFrame(const PwaLaunchInfo* pwa, BRect fallback)
{
	if (pwa == NULL || !pwa->installed || ManifestDisplayModeFor(
			pwa->display) != kManifestDisplayFullscreen)
		return fallback;

	BScreen screen(B_MAIN_SCREEN_ID);
	return screen.Frame();
}


static std::string
_PwaTitle(const PwaLaunchInfo* pwa)
{
	if (pwa != NULL && pwa->installed && !pwa->name.empty())
		return pwa->name;
	return std::string("NetPlus");
}


MainWindow::MainWindow(BRect frame, const char* url, bool privateWindow,
	const PwaLaunchInfo* pwa)
	:
	BWindow(_PwaFrame(pwa, frame), _PwaTitle(pwa).c_str(),
		B_TITLED_WINDOW, 0),
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
	fSavedFrame(frame),
	fAppMenu(nullptr),
	fLastOrigin(frame.LeftTop()),
	fSearchEngine(0),
	fPwaMode(pwa != NULL && pwa->installed),
	fPwaDisplay(kManifestDisplayStandalone),
	fStore(nullptr)
{
	if (fPwaMode) {
		fPwa = *pwa;
		fPwaDisplay = ManifestDisplayModeFor(pwa->display);
		if (fPwaDisplay == kManifestDisplayFullscreen)
			SetLook(B_NO_BORDER_WINDOW_LOOK);
	}

	BRect viewRect(0, 0, Bounds().Width(), Bounds().Height());

	fWebView   = new BWebView(viewRect, "webview");
	fWebView->SetTarget(BMessenger(this));
	fWebView->AddUserScript(kInstallScript, false);
	fWebView->AddScriptMessageHandler(kInstallHandler);

	if (!fPwaMode) {
		fMenuBar = new BMenuBar("menubar");

		BMenu* fileMenu = new BMenu("File");
		fileMenu->AddItem(new BMenuItem("Open Location" B_UTF8_ELLIPSIS,
			new BMessage(MSG_OPEN_LOCATION), 'L'));
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


		fToolbar   = new ToolbarView(viewRect, &fHistory);
		fStatusBar = new StatusBarView(viewRect);

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fMenuBar)
			.Add(fToolbar)
			.Add(fWebView, 1.0f)
			.Add(fStatusBar);
	} else if (fPwaDisplay == kManifestDisplayMinimalUi) {
		// minimal-ui keeps a back/reload strip and nothing else.
		BView* chrome = new BView(viewRect, "pwachrome", B_FOLLOW_NONE, 0);
		BButton* back = new BButton("Back", new BMessage(MSG_NAV_BACK));
		BButton* reload
			= new BButton("Reload", new BMessage(MSG_NAV_RELOAD));
		BLayoutBuilder::Group<>(chrome, B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(back)
			.Add(reload)
			.AddGlue();
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(chrome)
			.Add(fWebView, 1.0f);
	} else
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fWebView, 1.0f);

	BWebSettings* settings = NetPlusApp::Settings();

	fWebView->SetSession(NULL,
		fPrivateWindow || settings->PrivateBrowsing());
	_ApplySettings();
	if (!fPwaMode)
		_RebuildBookmarksMenu();

	const char* startup = (url != NULL && url[0] != '\0')
		? url : settings->Homepage();
	fWebView->LoadURL(startup);
}


MainWindow::~MainWindow()
{
	if (fAppMenu != NULL) {
		fAppMenu->Quit();
		fAppMenu = nullptr;
	}
	delete fStore;
	fStore = nullptr;
}


InstalledAppStore&
MainWindow::_Store()
{
	if (fStore == NULL) {
		BPath root;
		// A failed path leaves the store root empty: Add() fails
		// cleanly instead of scattering records.
		DeskbarEntry::WebAppStorePath(&root);
		fStore = new InstalledAppStore(root.Path());
	}
	return *fStore;
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
	fWebView->SetAutoplayPolicy(
		(web_autoplay_policy)settings->AutoplayPolicy());

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
	if (enter == fFullScreen) {
		fWebView->NotifyFullScreen(enter);
		return;
	}

	if (enter) {
		fSavedFrame = Frame();
		fSavedLook = Look();
		GetSizeLimits(&fSavedLimits[0], &fSavedLimits[1], &fSavedLimits[2],
			&fSavedLimits[3]);
		if (fMenuBar != NULL)
			fMenuBar->Hide();
		if (fToolbar != NULL)
			fToolbar->Hide();
		if (fStatusBar != NULL)
			fStatusBar->Hide();

		BRect screen = BScreen(this).Frame();
		SetLook(B_NO_BORDER_WINDOW_LOOK);
		SetSizeLimits(fSavedLimits[0], std::max(fSavedLimits[1],
			screen.Width()), fSavedLimits[2], std::max(fSavedLimits[3],
			screen.Height()));
		MoveTo(screen.left, screen.top);
		ResizeTo(screen.Width(), screen.Height());
	} else {
		SetLook(fSavedLook);
		SetSizeLimits(fSavedLimits[0], fSavedLimits[1], fSavedLimits[2],
			fSavedLimits[3]);
		MoveTo(fSavedFrame.left, fSavedFrame.top);
		ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
		if (fMenuBar != NULL)
			fMenuBar->Show();
		if (fToolbar != NULL)
			fToolbar->Show();
		if (fStatusBar != NULL)
			fStatusBar->Show();
	}
	fFullScreen = enter;

	// WebKit is told only once the view has its final size.
	Layout(true);
	Sync();
	fWebView->NotifyFullScreen(enter);
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
MainWindow::_AddHistory(const char* url)
{
	fHistory.Add(url);
}


void
MainWindow::FrameMoved(BPoint where)
{
	BWindow::FrameMoved(where);

	// Open panels stay anchored to their toolbar button.
	const BPoint delta = where - fLastOrigin;
	fLastOrigin = where;
	// Messengers, not the pointers: a closing panel may already be gone.
	BMessenger* panels[] = { &fAppMenuWindow };
	for (BMessenger* panel : panels) {
		BLooper* looper = NULL;
		if (panel->LockTargetWithTimeout(0) == B_OK
			&& panel->Target(&looper) != NULL) {
			BWindow* window = dynamic_cast<BWindow*>(looper);
			if (window != NULL)
				window->MoveBy(delta.x, delta.y);
			looper->Unlock();
		}
	}
}


void
MainWindow::_ShowAppMenu(BPoint anchor)
{
	if (fAppMenu != nullptr) {
		fAppMenu->PostMessage(B_QUIT_REQUESTED);
		return;
	}

	// Firefox/Chrome panel layout: window actions on top, page
	// actions with zoom next, History drills into a sub-page, the
	// application entries sit at the bottom.
	PanelMenuModel model;
	int32_t root = model.AddPage("Main menu");

	model.AddSectionLabelRow(root, "Tab and window");
	model.AddActionRow(root, "New Window", MSG_PANEL_NEW_WINDOW,
		"Ctrl+N");
	model.AddActionRow(root, "Open Location" B_UTF8_ELLIPSIS,
		MSG_OPEN_LOCATION, "Ctrl+L");
	model.AddSeparatorRow(root);

	model.AddSectionLabelRow(root, "Page");
	model.AddActionRow(root, "Back", MSG_NAV_BACK, "Alt+Left");
	model.AddActionRow(root, "Forward", MSG_NAV_FORWARD, "Alt+Right");
	model.AddActionRow(root, "Reload", MSG_NAV_RELOAD, "Ctrl+R");
	model.AddActionRow(root, "Home", MSG_NAV_HOME);
	model.AddSeparatorRow(root);

	model.AddSectionLabelRow(root, "Zoom");
	model.AddActionRow(root, "Zoom In", MSG_ZOOM_IN, "Ctrl++");
	model.AddActionRow(root, "Zoom Out", MSG_ZOOM_OUT, "Ctrl+-");
	model.AddActionRow(root, "Reset Zoom", MSG_ZOOM_RESET, "Ctrl+0");
	model.AddSeparatorRow(root);

	model.AddSectionLabelRow(root, "Search engine");
	for (int32 i = 0; i < CountSearchEngines(); i++) {
		char payload[16];
		snprintf(payload, sizeof(payload), "%d", i);
		const char* label = SearchEngineAt(i).name;
		// These pick the URL bar's search engine; mark the current one.
		model.AddActionRow(root, label, MSG_SET_ENGINE,
			i == fSearchEngine ? "\xe2\x9c\x93" : NULL, payload);
	}
	model.AddSeparatorRow(root);

	int32_t history = model.AddPage("History");
	model.AddSubPageRow(root, "History", history);
	const std::vector<std::string>& entries = fHistory.Entries();
	if (entries.empty())
		model.AddSectionLabelRow(history, "No recent pages");
	for (size_t i = 0; i < entries.size(); i++) {
		model.AddActionRow(history, entries[i].c_str(), MSG_OPEN_URL,
			nullptr, entries[i].c_str());
	}

	model.AddSeparatorRow(root);
	if (fManifest.IsValid()) {
		model.AddSectionLabelRow(root, "Web app");
		model.AddActionRow(root, "Install as app", MSG_INSTALL_APP);
		model.AddSeparatorRow(root);
	}

	model.AddSectionLabelRow(root, "Application");
	model.AddActionRow(root, "Downloads", MSG_SHOW_DOWNLOADS, "Ctrl+J");
	model.AddActionRow(root, "Settings" B_UTF8_ELLIPSIS, MSG_SHOW_SETTINGS);
	model.AddActionRow(root, "About NetPlus", B_ABOUT_REQUESTED);
	model.AddActionRow(root, "Quit NetPlus", B_QUIT_REQUESTED, "Ctrl+Q");

	fAppMenu = new PanelMenu(model, BMessenger(this));
	fAppMenuWindow = BMessenger(fAppMenu);
	fAppMenu->Go(anchor);
}


void
MainWindow::_HandleDownloadMessage(BMessage* message)
{
	uint64 id = 0;
	message->FindUInt64(B_WEB_DOWNLOAD_ID, &id);

	const char* name = "";
	const char* url = "";
	const char* destination = "";
	uint64 received = 0;
	uint64 total = 0;
	message->FindString(B_WEB_DOWNLOAD_FILENAME, &name);
	message->FindString(B_WEB_DOWNLOAD_URL, &url);
	message->FindString(B_WEB_DOWNLOAD_DESTINATION, &destination);
	message->FindUInt64(B_WEB_DOWNLOAD_RECEIVED, &received);
	message->FindUInt64(B_WEB_DOWNLOAD_TOTAL, &total);

	switch (message->what) {
		case B_WEB_DOWNLOAD_STARTED:
			fDownloadsModel.AddOrUpdate(id, name, url, destination,
				received, total);
			fDownloadsModel.MarkState(id, kDownloadStateActive);
			break;

		case B_WEB_DOWNLOAD_PROGRESS:
			fDownloadsModel.SetProgress(id, received, total);
			break;

		case B_WEB_DOWNLOAD_FINISHED:
		{
			int32 index = fDownloadsModel.IndexOf(id);
			uint64 size = index >= 0
				? fDownloadsModel.ItemAt(index).totalBytes : 0;
			fDownloadsModel.AddOrUpdate(id, name, url, destination,
				size, size);
			fDownloadsModel.MarkState(id, kDownloadStateFinished);
			break;
		}

		case B_WEB_DOWNLOAD_FAILED:
		{
			bool cancelled = false;
			message->FindBool(B_WEB_DOWNLOAD_CANCELLED, &cancelled);
			int32 index = fDownloadsModel.IndexOf(id);
			if (index < 0)
				fDownloadsModel.AddOrUpdate(id, name, url, "", 0, 0);
			fDownloadsModel.MarkState(id, cancelled
				? kDownloadStateCancelled : kDownloadStateFailed);
			break;
		}
	}

	_RefreshDownloadUI();
}


void
MainWindow::_RefreshDownloadUI()
{
	if (fToolbar != nullptr)
		fToolbar->SetDownloadsActive(fDownloadsModel.ActiveCount());
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_ABOUT_REQUESTED:
		{
			BAlert* alert = new BAlert("About NetPlus",
				"NetPlus Browser v0.5\n"
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

		case MSG_OPEN_LOCATION:
			if (fToolbar) fToolbar->FocusURL();
			break;

		case MSG_OPEN_URL:
		{
			// The URL bar and the history rows both pass the final
			// URL in "url".
			const char* url = nullptr;
			if (message->FindString("url", &url) == B_OK)
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

		case MSG_SET_ENGINE:
		{
			const char* payload = nullptr;
			if (message->FindString("url", &payload) != B_OK
				|| payload == nullptr)
				break;
			int32 engine = atoi(payload);
			if (engine < 0 || engine >= CountSearchEngines())
				break;
			fSearchEngine = engine;
			if (fToolbar != nullptr)
				fToolbar->SetSearchEngine(engine);
			break;
		}

		case MSG_APP_MENU:
		{
			BPoint anchor;
			if (message->FindPoint("anchor", &anchor) == B_OK)
				_ShowAppMenu(anchor);
			break;
		}

		case MSG_PANEL_CLOSED:
			fAppMenu = nullptr;
				// the panel deletes itself on quit
			break;

		case MSG_DOWNLOADS:
			DownloadsWindow::ShowWindow(fWebView);
			break;

		case MSG_PANEL_NEW_WINDOW:
		{
			const char* url = fWebView != nullptr
				? fWebView->CurrentURL() : nullptr;
			static_cast<NetPlusApp*>(be_app)->OpenWindow(
				(url != nullptr && url[0] != '\0') ? url : kHomePage,
				fPrivateWindow);
			break;
		}

		case MSG_ZOOM_IN:
		case MSG_ZOOM_OUT:
		case MSG_ZOOM_RESET:
		{
			if (fWebView == nullptr)
				break;
			float zoom = fWebView->ZoomLevel();
			if (message->what == MSG_ZOOM_RESET)
				zoom = 1.0f;
			else if (message->what == MSG_ZOOM_IN)
				zoom = std::min(zoom * 1.25f, 5.0f);
			else
				zoom = std::max(zoom / 1.25f, 0.2f);
			fWebView->SetZoomLevel(zoom);
			break;
		}

		case B_WEB_PAGE_TITLE_CHANGED:
		{
			const char* title = nullptr;
			if (message->FindString(B_WEB_TITLE_FIELD, &title) == B_OK
				&& title != nullptr && title[0] != '\0') {
				// Installed apps keep the manifest name in the title bar.
				if (!fPwaMode) {
					BString windowTitle("NetPlus - ");
					windowTitle << title;
					SetTitle(windowTitle.String());
				}
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
				_AddHistory(url);
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
			_HandleDownloadMessage(message);
			break;

		case B_WEB_SCRIPT_MESSAGE:
		{
			const char* handler = nullptr;
			if (message->FindString(B_WEB_HANDLER_FIELD, &handler)
				!= B_OK || handler == nullptr)
				break;
			if (strcmp(handler, "netplus-manifest") == 0)
				_HandleManifestMessage(message);
			else if (strcmp(handler, "netplus-icon") == 0)
				_HandleIconMessage(message);
			break;
		}

		case MSG_INSTALL_APP:
			_StartInstall();
			break;

		case MSG_INSTALL_CONFIRM:
		{
			int32 which = -1;
			message->FindInt32("which", &which);
			if (which != 0 || fWebView == nullptr)
				break;

			// Icon goes through the page's own fetch so cookies
			// and the proxy apply; the reply lands as a handler
			// message with the bytes in base64.
			const ManifestIcon* icon
				= PickBestIcon(fManifest, 512 * 512);
			if (icon == nullptr) {
				_FinishInstall(std::string());
				break;
			}
			std::string iconUrl = ResolveUrl(
				fManifestUrl.empty() ? fManifestPage : fManifestUrl,
				icon->src);
			if (iconUrl.empty()) {
				_FinishInstall(std::string());
				break;
			}
			std::string script = "window.__netplusFetchIcon('";
			for (size_t i = 0; i < iconUrl.size(); i++) {
				char c = iconUrl[i];
				if (c == '\'' || c == '\\')
					script.push_back('\\');
				script.push_back(c);
			}
			script.append("')");
			fWebView->EvaluateJavaScript(script.c_str(),
				BMessenger(this), MSG_ICON_FETCH_JS);
			break;
		}

		case MSG_ICON_FETCH_JS:
			// Trigger only; the bytes arrive via the handler.
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MainWindow::_HandleManifestMessage(BMessage* message)
{
	const char* body = nullptr;
	if (message->FindString(B_WEB_BODY_FIELD, &body) != B_OK
		|| body == nullptr)
		return;

	size_t length = strlen(body);
	std::string url;
	std::string page;
	std::string text;
	if (!ExtractJsonString(body, length, "url", &url)
		|| !ExtractJsonString(body, length, "page", &page)
		|| !ExtractJsonString(body, length, "text", &text))
		return;

	WebAppManifest manifest;
	if (!manifest.Parse(text.c_str(), text.size()))
		return;
	// Nothing to install without a start_url to relaunch at.
	if (manifest.startUrl.empty())
		return;

	fManifest = manifest;
	fManifestUrl = url;
	fManifestPage = page;
}


void
MainWindow::_HandleIconMessage(BMessage* message)
{
	const char* body = nullptr;
	if (message->FindString(B_WEB_BODY_FIELD, &body) != B_OK
		|| body == nullptr)
		return;

	size_t length = strlen(body);
	std::string data;
	if (!ExtractJsonString(body, length, "data", &data) || data.empty()) {
		// No icon this time; install still works, the entry just
		// carries NetPlus's icon.
		_FinishInstall(std::string());
		return;
	}

	std::vector<uint8_t> bytes;
	if (!Base64Decode(data.c_str(), data.size(), &bytes)
		|| bytes.empty()) {
		_FinishInstall(std::string());
		return;
	}

	// Cache the raw bytes as PNG: the Deskbar entry stamps the node
	// icon from this file, the record points at it afterwards.
	InstalledAppStore& store = _Store();
	if (store.Root().empty()) {
		_FinishInstall(std::string());
		return;
	}

	std::string id = InstalledAppStore::IDFromName(
		(fManifest.name.empty() ? fManifest.shortName : fManifest.name)
			.c_str());
	if (id.empty())
		id = "app";
	std::string iconPath = store.IconsDirectory() + "/" + id + ".png";
	FILE* file = fopen(iconPath.c_str(), "wb");
	if (file == NULL) {
		_FinishInstall(std::string());
		return;
	}
	fwrite(bytes.data(), 1, bytes.size(), file);
	fclose(file);
	_FinishInstall(iconPath);
}


void
MainWindow::_StartInstall()
{
	if (!fManifest.IsValid() || fManifest.startUrl.empty())
		return;

	std::string startUrl = ResolveUrl(
		fManifestUrl.empty() ? fManifestPage : fManifestUrl,
		fManifest.startUrl);
	std::string name = fManifest.name.empty() ? fManifest.shortName
		: fManifest.name;
	if (name.empty() || startUrl.empty())
		return;

	BString text("Install ");
	text << name.c_str() << " as an app?\n\nStarts at: "
		<< startUrl.c_str();

	BAlert* alert = new BAlert("Install app", text.String(), "Install",
		"Cancel", NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go(new BInvoker(new BMessage(MSG_INSTALL_CONFIRM),
		BMessenger(this)));
}


void
MainWindow::_FinishInstall(const std::string& iconPath)
{
	std::string name = fManifest.name.empty() ? fManifest.shortName
		: fManifest.name;
	std::string startUrl = ResolveUrl(
		fManifestUrl.empty() ? fManifestPage : fManifestUrl,
		fManifest.startUrl);
	if (name.empty() || startUrl.empty())
		return;

	InstalledApp record;
	record.id = InstalledAppStore::IDFromName(name.c_str());
	record.name = name;
	record.startUrl = startUrl;
	record.display = fManifest.display;
	record.manifestUrl = fManifestUrl;
	record.iconPath = iconPath;

	InstalledAppStore& store = _Store();
	if (!store.Add(record)) {
		fprintf(stderr, "NetPlus: could not write the app record\n");
		return;
	}

	std::string self = _SelfPath();
	if (self.empty()) {
		fprintf(stderr, "NetPlus: could not resolve own path\n");
		return;
	}

	status_t error = DeskbarEntry::Create(name.c_str(), self.c_str(),
		iconPath.empty() ? NULL : iconPath.c_str());
	if (error != B_OK) {
		fprintf(stderr, "NetPlus: Deskbar entry failed: %ld\n",
			(long)error);
		return;
	}

	BString done("Installed ");
	done << name.c_str();
	BAlert* alert = new BAlert("Install app", done.String(), "OK", NULL,
		NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go();
}
