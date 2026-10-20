/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "DeskbarEntry.h"
#include "InstalledAppStore.h"
#include "MainWindow.h"
#include "NetPlusApp.h"
#include "NetPlusMessages.h"

#include <Alert.h>
#include <Entry.h>
#include <Messenger.h>
#include <Path.h>
#include <String.h>

#include <web/UrlNormalize.h>
#include <web/WebDefs.h>
#include <web/WebView.h>
#include <web/WebBookmarks.h>
#include <web/WebSettings.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>


static const char* kSettingsFileName = "settings";
static const char* kBookmarksFileName = "bookmarks.html";
static const char* kLastURLKey = "last_session_url";

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


static std::string
_BaseName(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}


static bool
_LookupInstalled(const char* entryName, InstalledApp* into)
{
	BPath root;
	DeskbarEntry::WebAppStorePath(&root);
	InstalledAppStore store(root.Path());
	int32_t index = store.IndexOfEntryName(entryName);
	if (index < 0)
		return false;
	*into = store.At(index);
	return true;
}


NetPlusApp::NetPlusApp()
	:
	BApplication("application/x-vnd.Vitruvian-NetPlus"),
	fSettings(new BWebSettings()),
	fBookmarks(new BWebBookmarks()),
	fWindowCount(0)
{
}


NetPlusApp::~NetPlusApp()
{
	delete fBookmarks;
	delete fSettings;
}


/* static */
BWebSettings*
NetPlusApp::Settings()
{
	return static_cast<NetPlusApp*>(be_app)->fSettings;
}


/* static */
BWebBookmarks*
NetPlusApp::Bookmarks()
{
	return static_cast<NetPlusApp*>(be_app)->fBookmarks;
}


/* static */
const char*
NetPlusApp::SettingsDir()
{
	static BString dir;
	if (dir.IsEmpty()) {
		const char* home = getenv("HOME");
		dir << (home != NULL ? home : "/tmp")
			<< "/config/settings/NetPlus";
	}
	return dir.String();
}


/* static */
const char*
NetPlusApp::SearchTemplateFor(const char* engine)
{
	if (engine == NULL)
		return kWebSearchDuckDuckGo;
	if (strcmp(engine, "Startpage") == 0)
		return kWebSearchStartpage;
	if (strcmp(engine, "Brave Search") == 0)
		return kWebSearchBrave;
	if (strcmp(engine, "Google") == 0)
		return kWebSearchGoogle;
	if (strcmp(engine, "Bing") == 0)
		return kWebSearchBing;
	if (strcmp(engine, "Wikipedia") == 0)
		return kWebSearchWikipedia;
	// Custom and unknown engines use the stored template.
	return NULL;
}


void
NetPlusApp::_CreateDirs()
{
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::create_directories(std::string(SettingsDir()) + "/profile", ec);
}


void
NetPlusApp::_LoadSettings()
{
	BString path(SettingsDir());
	path << "/" << kSettingsFileName;
	fSettings->Load(path.String());

	BWebView::SetDefaultProfileDir(
		(std::string(SettingsDir()) + "/profile").c_str());
}


void
NetPlusApp::_LoadBookmarks()
{
	BString path(SettingsDir());
	path << "/" << kBookmarksFileName;
	fBookmarks->Load(path.String());
}


void
NetPlusApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--pwa=", 6) == 0) {
			InstalledApp app;
			if (_LookupInstalled(argv[i] + 6, &app)) {
				fPwa.installed = true;
				fPwa.id = app.id;
				fPwa.name = app.name;
				fPwa.startUrl = app.startUrl;
				fPwa.display = app.display;
				fPwa.iconPath = app.iconPath;
			}
			continue;
		}
		if (argv[i][0] == '-')
			continue;
		// A link opened while running (e.g. /bin/open) gets a window.
		if (fReady)
			OpenWindow(argv[i], fSettings->PrivateBrowsing());
		else if (fStartupURL.IsEmpty())
			fStartupURL = argv[i];
	}
}


void
NetPlusApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_WEB_DOWNLOAD_STARTED:
		case B_WEB_DOWNLOAD_PROGRESS:
		case B_WEB_DOWNLOAD_FINISHED:
		case B_WEB_DOWNLOAD_FAILED:
		{
			// Not tied to a view: the first browser window records it.
			for (int32 i = 0; i < CountWindows(); i++) {
				MainWindow* window = dynamic_cast<MainWindow*>(WindowAt(i));
				if (window != NULL) {
					BMessenger(window).SendMessage(message);
					break;
				}
			}
			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
NetPlusApp::RefsReceived(BMessage* message)
{
	// The Deskbar entry launches by refs: the symlink's name is the
	// installed app's name.
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK)
		return;

	InstalledApp app;
	if (!_LookupInstalled(ref.name, &app)) {
		// A document (an .html file): open it as a file URL.
		BPath path(&ref);
		if (path.InitCheck() != B_OK)
			return;
		BString url("file://");
		url << path.Path();
		if (fReady)
			OpenWindow(url.String(), fSettings->PrivateBrowsing());
		else if (fStartupURL.IsEmpty())
			fStartupURL = url;
		return;
	}

	fPwa.installed = true;
	fPwa.id = app.id;
	fPwa.name = app.name;
	fPwa.startUrl = app.startUrl;
	fPwa.display = app.display;
	fPwa.iconPath = app.iconPath;

	// Launch refs land before ReadyToRun; only a second launch while
	// running needs its own window here.
	if (fReady)
		OpenWindow(fPwa.startUrl.c_str(), false, &fPwa);
}


void
NetPlusApp::ReadyToRun()
{
	_CreateDirs();
	_LoadSettings();
	_LoadBookmarks();

	// Every window shares one profile session: route its download
	// events through the app so they arrive once, not once per window.
	BWebView::SetDownloadTarget(BMessenger(this));

	if (!fPwa.installed) {
		// A menu symlink launch delivers argv[0] as the link path.
		InstalledApp app;
		std::string base = _BaseName(_SelfPath());
		if (!base.empty() && _LookupInstalled(base.c_str(), &app)) {
			fPwa.installed = true;
			fPwa.id = app.id;
			fPwa.name = app.name;
			fPwa.startUrl = app.startUrl;
			fPwa.display = app.display;
			fPwa.iconPath = app.iconPath;
		}
	}

	fReady = true;
	if (fPwa.installed) {
		OpenWindow(fPwa.startUrl.c_str(), false, &fPwa);
		return;
	}

	const char* url = fStartupURL.String();
	BString restoreURL;
	if (url[0] == '\0' && fSettings->RestoreSession()) {
		auto it = fSettings->Values().find(kLastURLKey);
		if (it != fSettings->Values().end() && !it->second.empty())
			restoreURL = it->second.c_str();
		url = restoreURL.String();
	}

	OpenWindow(url, fSettings->PrivateBrowsing());
}


void
NetPlusApp::OpenWindow(const char* url, bool privateWindow,
	const PwaLaunchInfo* pwa)
{
	BRect frame(100, 100, 1100, 800);
	MainWindow* window = new MainWindow(frame, url, privateWindow, pwa);
	fWindowCount++;
	window->Show();
}


void
NetPlusApp::WindowClosed(MainWindow* window)
{
	(void)window;
	fWindowCount--;
	if (fWindowCount <= 0)
		be_app->PostMessage(B_QUIT_REQUESTED);
}


void
NetPlusApp::BroadcastSettingsChanged()
{
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window != NULL)
			window->PostMessage(MSG_SETTINGS_CHANGED);
	}
}


void
NetPlusApp::BroadcastBookmarksChanged()
{
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window != NULL)
			window->PostMessage(MSG_BOOKMARKS_CHANGED);
	}
}


int
main(int /*argc*/, char** /*argv*/)
{
	NetPlusApp app;
	app.Run();
	return 0;
}
