/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ProfileNameWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


static const uint32 kMsgSave = 'pnSv';
static const uint32 kMsgCancel = 'pnCn';
static const uint32 kMsgNameChanged = 'pnCh';


ProfileNameWindow::ProfileNameWindow(BWindow* parent, const char* suggestion,
	const BMessenger& target, uint32 messageWhat)
	:
	BWindow(BRect(0, 0, 300, 100), B_TRANSLATE("Save profile"),
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fWhat(messageWhat)
{
	fNameControl = new BTextControl("name", B_TRANSLATE("Name:"), suggestion,
		new BMessage(kMsgNameChanged));
	fNameControl->SetModificationMessage(new BMessage(kMsgNameChanged));

	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));
	fSaveButton = new BButton("save", B_TRANSLATE("Save"),
		new BMessage(kMsgSave));
	fSaveButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fNameControl)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(fSaveButton)
		.End();

	_UpdateSaveButton();
	fNameControl->MakeFocus(true);
	fNameControl->TextView()->SelectAll();

	if (parent != NULL) {
		AddToSubset(parent);
		CenterIn(parent->Frame());
	} else
		CenterOnScreen();
}


void
ProfileNameWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgNameChanged:
			_UpdateSaveButton();
			break;

		case kMsgSave:
		{
			BString name(fNameControl->Text());
			name.Trim();
			if (name.Length() == 0)
				break;
			BMessage reply(fWhat);
			reply.AddString("profile_name", name.String());
			fTarget.SendMessage(&reply);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case kMsgCancel:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
ProfileNameWindow::_UpdateSaveButton()
{
	BString name(fNameControl->Text());
	name.Trim();
	fSaveButton->SetEnabled(name.Length() > 0);
}
