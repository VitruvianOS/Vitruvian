/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WebMailApp.h"
#include "MainWindow.h"
#include "WebMailMessages.h"

#include <Path.h>
#include <String.h>
#include <WebView.h>

#include <cstring>
#include <filesystem>


WebMailApp::WebMailApp()
	:
	BApplication("application/x-vnd.vos-webmail"),
	fWindow(NULL)
{
}


WebMailApp::~WebMailApp()
{
}


/* static */
const char*
WebMailApp::UiDirectory()
{
	static BString directory;
	if (directory.Length() > 0)
		return directory.String();

	// The bundled pages live in ui/ next to the executable; the
	// installed layout is /system/apps/WebMail/ui.
	char exePath[1024];
	ssize_t length = readlink("/proc/self/exe", exePath,
		sizeof(exePath) - 1);
	if (length > 0) {
		exePath[length] = '\0';
		std::filesystem::path path(exePath);
		std::filesystem::path candidate = path.parent_path() / "ui";
		std::error_code ec;
		if (std::filesystem::is_directory(candidate, ec)) {
			directory = candidate.string().c_str();
			return directory.String();
		}
	}
	directory = "/system/apps/WebMail/ui";
	return directory.String();
}


void
WebMailApp::ReadyToRun()
{
	// The vos-mail: scheme serves the native mail UI; register it
	// before any view attaches to a window.
	BWebView::SetLocalSchemeDirectory("vos-mail",
		UiDirectory());
	OpenWindow();
}


void
WebMailApp::OpenWindow()
{
	if (fWindow != NULL) {
		fWindow->Activate();
		return;
	}
	BRect frame(80, 80, 1180, 820);
	fWindow = new MainWindow(frame);
	fWindow->Show();
}


void
WebMailApp::WindowClosed(MainWindow* window)
{
	if (window == fWindow)
		fWindow = NULL;
	be_app->PostMessage(B_QUIT_REQUESTED);
}


void
WebMailApp::RefsReceived(BMessage* message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BPath path(&ref);
		if (path.InitCheck() != B_OK)
			continue;
		BString url = path.Path();
		if (url.IFindFirst("mailto:") == 0)
			_HandleMailto(url.String());
	}
}


void
WebMailApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strncmp(argv[i], "mailto:", 7) == 0)
			_HandleMailto(argv[i]);
	}
}


void
WebMailApp::_HandleMailto(const char* url)
{
	if (fWindow == NULL)
		OpenWindow();
	if (fWindow == NULL)
		return;
	BMessage message(MSG_OPEN_MAILTO);
	message.AddString(WEBMAIL_URL_FIELD, url);
	fWindow->PostMessage(&message);
}
