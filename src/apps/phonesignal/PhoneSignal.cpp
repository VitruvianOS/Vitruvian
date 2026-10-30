/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Double-click entry point: a small window hosting the replicant view.
// The Deskbar loads the same binary through instantiate_deskbar_item().

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Entry.h>
#include <Locale.h>
#include <String.h>
#include <Window.h>

#include <image.h>
#include <stdio.h>
#include <string.h>

#include "PhoneSignalView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PhoneSignal"


// Locates our own executable image: the path is what the Deskbar loads
// when installing the tray item, and it cannot come from be_app resources.
static status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char*)our_image >= (char*)image.text
			&& (char*)our_image <= (char*)image.text + image.text_size)
			return B_OK;
	}

	return B_ERROR;
}


class PhoneSignal : public BApplication {
public:
	PhoneSignal();

	virtual void	ArgvReceived(int32 argc, char** argv);
	virtual void	ReadyToRun();

private:
	status_t		_InstallReplicantInDeskbar();

	bool			fAutoInstallInDeskbar;
	bool			fQuitImmediately;
};


PhoneSignal::PhoneSignal()
	:
	BApplication(PhoneSignalView::kAppSignature),
	fAutoInstallInDeskbar(false),
	fQuitImmediately(false)
{
}


void
PhoneSignal::ArgvReceived(int32 argc, char** argv)
{
	if (argc <= 1)
		return;

	if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		printf("PhoneSignal options:\n"
			"\t--deskbar\tautomatically add replicant to Deskbar\n"
			"\t--help\t\tprint this info and exit\n");
		fQuitImmediately = true;
		return;
	}

	if (strcmp(argv[1], "--deskbar") == 0) {
		fAutoInstallInDeskbar = true;
		// A second instance forwards its argv while the first one launches.
		if (!IsLaunching()) {
			BDeskbar deskbar;
			if (deskbar.IsRunning()
				&& !deskbar.HasItem(PhoneSignalView::kDeskbarItemName)) {
				_InstallReplicantInDeskbar();
			}
		}
	}
}


void
PhoneSignal::ReadyToRun()
{
	if (fQuitImmediately) {
		Quit();
		return;
	}

	bool isDeskbarRunning = true;
	bool isInstalled = false;

	{
		BDeskbar deskbar;
		isDeskbarRunning = deskbar.IsRunning();
		isInstalled = deskbar.HasItem(PhoneSignalView::kDeskbarItemName);
	}

	if (fAutoInstallInDeskbar) {
		if (isInstalled) {
			Quit();
			return;
		}
		// The session can come up in any order relative to us.
		int32 tries = 10;
		while (!isDeskbarRunning && --tries) {
			BDeskbar deskbar;
			if (deskbar.IsRunning()) {
				isDeskbarRunning = true;
				break;
			}
			snooze(1000000);
		}
		if (!isDeskbarRunning) {
			puts("Deskbar is not running, giving up.");
			Quit();
			return;
		}

		_InstallReplicantInDeskbar();
		Quit();
		return;
	}

	if (isDeskbarRunning && !isInstalled) {
		BString text = B_TRANSLATE(
			"You can run PhoneSignal in a window or install it in the Deskbar.");
		BAlert* alert = new BAlert("", text,
			B_TRANSLATE("Run in window"), B_TRANSLATE("Install in Deskbar"),
			NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);

		if (alert->Go() == 1) {
			_InstallReplicantInDeskbar();
			Quit();
			return;
		}
	}

	// Single-window utility: closing the window quits the app.
	BWindow* window = new BWindow(BRect(120, 120, 443, 213),
		"Phone signal", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE);
	// This keymap puts Command on Control, so Command+Q is Ctrl+Q; add the
	// plain Alt+Q spelling too, the one users reach for first.
	window->AddShortcut('Q', B_OPTION_KEY, new BMessage(B_QUIT_REQUESTED));
	window->AddChild(new PhoneSignalView(window->Bounds(),
		B_FOLLOW_ALL_SIDES, false));
	window->Show();
}


status_t
PhoneSignal::_InstallReplicantInDeskbar()
{
	image_info info;
	entry_ref ref;

	if (our_image(info) == B_OK
		&& get_ref_for_path(info.name, &ref) == B_OK) {
		BDeskbar deskbar;
		return deskbar.AddItem(&ref);
	}

	return B_ERROR;
}


int
main()
{
	PhoneSignal app;
	app.Run();

	return 0;
}
