/*
 * Copyright 2001-2015, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Augustin Cavalier <waddlesplash>
 */


#include "AlertWindow.h"
#include "Constants.h"

#include <Button.h>
#include <Catalog.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>
#include <TimeUnitFormat.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


// Matches the app_server deadline minus its grace period: the dialog
// always resolves first when it is alive.
static const int32 kCountdownSeconds = 6;


AlertWindow::AlertWindow(BMessenger handler)
	: BAlert(B_TRANSLATE("Confirm changes"),
			 "", B_TRANSLATE("Undo"), B_TRANSLATE("Keep")),
	fSeconds(kCountdownSeconds),
	fResolved(false),
	fHandler(handler)
{
	SetType(B_WARNING_ALERT);
	SetPulseRate(1000000);
	TextView()->SetStylable(true);
	TextView()->GetFontAndColor(0, &fOriginalFont);
	fFont = fOriginalFont;
	fFont.SetFace(B_BOLD_FACE);
	UpdateCountdownView();
}


void
AlertWindow::ResetCountdown()
{
	fSeconds = kCountdownSeconds;
	UpdateCountdownView();
}


void
AlertWindow::Dismiss()
{
	fResolved = true;
	PostMessage(B_QUIT_REQUESTED);
	Hide();
}


void
AlertWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_PULSE) {
		if (--fSeconds == 0)
			Resolve(BUTTON_UNDO_MSG);
		else
			UpdateCountdownView();
	}

	BAlert::DispatchMessage(message, handler);
}


void
AlertWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'ALTB': // alert button message
		{
			int32 which;
			if (message->FindInt32("which", &which) == B_OK) {
				if (which == 1)
					Resolve(MAKE_INITIAL_MSG);
				else if (which == 0)
					Resolve(BUTTON_UNDO_MSG);
			}
			break;
		}

		case B_QUIT_REQUESTED:
			// The close button reverts; the quit posted by Resolve()
			// comes back here resolved and must complete the close.
			if (!fResolved) {
				Resolve(BUTTON_UNDO_MSG);
				break;
			}
			BAlert::MessageReceived(message);
			break;

		case B_KEY_DOWN:
		{
			int8 val;
			if (message->FindInt8("byte", &val) == B_OK
				&& val == B_ESCAPE) {
				Resolve(BUTTON_UNDO_MSG);
				break;
			}
			// fall through
		}

		default:
			BAlert::MessageReceived(message);
			break;
	}
}


void
AlertWindow::Resolve(uint32 what)
{
	if (fResolved)
		return;

	fResolved = true;
	fHandler.SendMessage(what);
	PostMessage(B_QUIT_REQUESTED);
	Hide();
}


void
AlertWindow::UpdateCountdownView()
{
	BString str1 = B_TRANSLATE("Do you wish to keep these settings?");
	BString string = str1;
	string += "\n";
	string += B_TRANSLATE("Settings will revert in %seconds.");

	BTimeUnitFormat format;
	BString tmp;
	format.Format(tmp, fSeconds, B_TIME_UNIT_SECOND);

	string.ReplaceFirst("%seconds", tmp);
	// The below is black magic, do not touch. We really need to refactor
	// BTextView sometime...
	TextView()->SetFontAndColor(0, str1.Length() + 1, &fOriginalFont,
		B_FONT_ALL);
	TextView()->SetText(string.String());
	TextView()->SetFontAndColor(0, str1.Length(), &fFont, B_FONT_ALL);
}
