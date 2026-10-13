/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "MainWindow.h"
#include "NetPlusApp.h"
#include "NetPlusMessages.h"

#include <Alert.h>
#include <Path.h>
#include <String.h>

#include <web/UrlNormalize.h>
#include <web/WebView.h>
#include <web/WebBookmarks.h>
#include <web/WebSettings.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>


static const char* kSettingsFileName = "settings";
static const char* kBookmarksFileName = "bookmarks.html";
static const char* kLastURLKey = "last_session_url";


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
		if (fStartupURL.IsEmpty() && argv[i][0] != '-')
			fStartupURL = argv[i];
	}
}


void
NetPlusApp::ReadyToRun()
{
	_CreateDirs();
	_LoadSettings();
	_LoadBookmarks();

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
NetPlusApp::OpenWindow(const char* url, bool privateWindow)
{
	BRect frame(100, 100, 1100, 800);
	MainWindow* window = new MainWindow(frame, url, privateWindow);
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
