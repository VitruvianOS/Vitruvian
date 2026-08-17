/*
 * Copyright 2026, Vitruvian Project.
 * AudioMixer standalone app + deskbar installer.
 * Distributed under the terms of the MIT License.
 */

#include "AudioMixerStatus.h"
#include "AudioMixerView.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Entry.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <String.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AudioMixerApp"


class AudioMixerWindow : public BWindow {
public:
	AudioMixerWindow(BRect frame)
		:
		BWindow(frame, B_TRANSLATE_SYSTEM_NAME("AudioMixer"),
			B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS)
	{
		SetLayout(new BGroupLayout(B_VERTICAL));
		AudioMixerView* view = new AudioMixerView(Bounds().InsetByCopy(8, 8),
			B_FOLLOW_ALL, false);
		AddChild(view);
		CenterOnScreen();
	}

	bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}
};








static status_t
InstallInDeskbar()
{
	BDeskbar deskbar;
	if (deskbar.HasItem(kDeskbarItemName))
		return B_OK;

	image_info info;
	entry_ref ref;
	status_t status = our_image(info);
	if (status == B_OK)
		status = get_ref_for_path(info.name, &ref);
	if (status == B_OK)
		status = deskbar.AddItem(&ref);

	if (status != B_OK) {
		BString message;
		message.SetToFormat(
			B_TRANSLATE("Installing %s in Deskbar failed:\n%s"),
			B_TRANSLATE_SYSTEM_NAME("AudioMixer"), strerror(status));
		BAlert* alert = new(std::nothrow) BAlert(B_TRANSLATE("Error"),
			message.String(), B_TRANSLATE("OK"));
		if (alert != NULL)
			alert->Go();
	}
	return status;
}


class AudioMixerApp : public BApplication {
public:
	AudioMixerApp()
		:
		BApplication(kSignature),
		fAutoDeskbar(false),
		fQuitAfterHelp(false),
		fExitCode(B_OK)
	{
	}

	status_t ExitCode() const { return fExitCode; }

	void ArgvReceived(int32 argc, char** argv) override
	{
		for (int32 i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--deskbar") == 0) {
				fAutoDeskbar = true;
			} else if (strcmp(argv[i], "--help") == 0
				|| strcmp(argv[i], "-h") == 0) {
				puts(B_TRANSLATE("AudioMixer options:\n"
					"\t--deskbar\tinstall replicant in Deskbar and quit\n"
					"\t--help\t\tprint this info and exit"));
				fQuitAfterHelp = true;
				return;
			}
		}
	}

	void ReadyToRun() override
	{
		if (fQuitAfterHelp) {
			Quit();
			return;
		}
		if (fAutoDeskbar) {
			fExitCode = InstallInDeskbar();
			Quit();
			return;
		}

		BDeskbar deskbar;
		if (deskbar.IsRunning() && !deskbar.HasItem(kDeskbarItemName)) {
			BString text = B_TRANSLATE(
				"You can run %appname% in a window or install it in the Deskbar.");
			text.ReplaceFirst("%appname%",
				B_TRANSLATE_SYSTEM_NAME("AudioMixer"));
			BAlert* alert = new(std::nothrow) BAlert("", text.String(),
				B_TRANSLATE("Run in window"),
				B_TRANSLATE("Install in Deskbar"), NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			if (alert != NULL && alert->Go() == 1) {
				fExitCode = InstallInDeskbar();
				Quit();
				return;
			}
		}

		(new AudioMixerWindow(BRect(100, 100, 180, 180)))->Show();
	}

private:
	bool fAutoDeskbar;
	bool fQuitAfterHelp;
	status_t fExitCode;
};


int
main(int, char**)
{
	AudioMixerApp app;
	app.Run();



	return app.ExitCode();
}
