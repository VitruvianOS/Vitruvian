/*
 * Copyright 2003-2013 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, jerome.duval@free.fr
 *		Julun, host.haiku@gmx.de
 *		Michael Phipps
 *		John Scipione, jscipione@gmail.com
 *		Dario Casalinuovo
 */


#include "PasswordWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <Screen.h>
#include <Size.h>
#include <StringView.h>

#include "ScreenSaverSettings.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ScreenSaver"


static const uint32 kMsgDone = 'done';


PasswordWindow::PasswordWindow(ScreenSaverSettings& settings)
	:
	BWindow(BRect(100, 100, 360, 180), B_TRANSLATE("Password Window"),
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSettings(settings)
{
	_Setup();
	Update();
}


void
PasswordWindow::_Setup()
{
	// Lock always uses the account password via janus; there is no custom one.
	fSettings.SetLockMethod("system");
	fSettings.SetPassword("");

	BView* topView = new BView("topView", B_WILL_DRAW);
	topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BStringView* info = new BStringView("info",
		B_TRANSLATE("Unlock uses the password of this account "
			"(V\\OS screen lock)."));
	info->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton* doneButton = new BButton("done", B_TRANSLATE("Done"),
		new BMessage(kMsgDone));

	BLayoutBuilder::Group<>(topView, B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(info)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(doneButton)
			.End()
		.End();

	doneButton->MakeDefault(true);

	SetLayout(new BGroupLayout(B_VERTICAL));
	GetLayout()->AddView(topView);
}


void
PasswordWindow::Update()
{
	fSettings.SetLockMethod("system");
	fSettings.SetPassword("");
}


void
PasswordWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDone:
			fSettings.SetLockMethod("system");
			fSettings.SetPassword("");
			fSettings.Save();
			Hide();
			break;

		case B_CANCEL:
			Hide();
			break;

		default:
			BWindow::MessageReceived(message);
 	}
}
