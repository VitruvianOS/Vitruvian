/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PasswordDialog.h"

#include <Alert.h>
#include <Button.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <new>


static const uint32 kMessageCancel = 'PwCn';
static const uint32 kMessageOK = 'PwOk';


class PasswordView : public BView {
public:
								PasswordView(const char* message,
									bool confirm)
									:
									BView("passwordView", B_WILL_DRAW),
									fConfirm(confirm)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

		fMessage = new BTextView("message");
		fMessage->SetText(message);
		fMessage->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		fMessage->MakeEditable(false);
		fMessage->MakeSelectable(false);
		fMessage->SetWordWrap(true);

		fPassword = new BTextControl("Password:", "", NULL);

		fConfirmControl = NULL;
		if (fConfirm)
			fConfirmControl = new BTextControl("Confirm:", "", NULL);

		fCancelButton = new BButton("Cancel", new BMessage(kMessageCancel));
		fOKButton = new BButton("OK", new BMessage(kMessageOK));

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0.0f)
			.AddStrut(8.0f)
			.Add(fMessage)
			.AddStrut(8.0f)
			.Add(fPassword)
			.Add(fConfirmControl)
			.AddStrut(8.0f)
			.AddGroup(B_HORIZONTAL, 0.0f)
				.Add(fCancelButton)
				.AddGlue()
				.Add(fOKButton)
			.End()
			.AddStrut(8.0f);
	}

	virtual	void				AttachedToWindow()
	{
		fCancelButton->SetTarget(Window());
		fOKButton->SetTarget(Window());
		fOKButton->MakeDefault(true);
		fPassword->MakeFocus();
	}

			status_t			Complete(BString& password)
			{
				if (fConfirm
					&& strcmp(fPassword->Text(), fConfirmControl->Text())
						!= 0) {
					BAlert* alert = new BAlert("passwords",
						"The passwords do not match.", "OK");
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go();
					fConfirmControl->SetText("");
					fConfirmControl->MakeFocus();
					return B_MISMATCHED_VALUES;
				}

				password = fPassword->Text();
				return B_OK;
			}

private:
			BTextView*			fMessage;
			BTextControl*		fPassword;
			BTextControl*		fConfirmControl;
			BButton*			fCancelButton;
			BButton*			fOKButton;
			bool				fConfirm;
};


PasswordDialog::PasswordDialog(const char* title, const char* message,
	bool confirm)
	:
	BWindow(BRect(50, 50, 100, 100), title, B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fView(NULL),
	fDoneSem(-1),
	fResult(B_CANCELED),
	fConfirm(confirm)
{
	fDoneSem = create_sem(0, "password dialog");
	if (fDoneSem < 0)
		return;

	BLayout* layout = new BGroupLayout(B_HORIZONTAL);
	if (layout == NULL)
		return;
	SetLayout(layout);

	fView = new PasswordView(message, confirm);
	if (fView == NULL)
		return;
	layout->AddView(fView);
}


PasswordDialog::~PasswordDialog()
{
	if (fDoneSem >= 0)
		delete_sem(fDoneSem);
}


bool
PasswordDialog::QuitRequested()
{
	fResult = B_CANCELED;
	release_sem(fDoneSem);
	return false;
}


void
PasswordDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMessageCancel:
			fResult = B_CANCELED;
			release_sem(fDoneSem);
			return;

		case kMessageOK:
		{
			BString password;
			status_t result = fView->Complete(password);
			if (result == B_OK) {
				fPassword = password;
				fResult = B_OK;
				release_sem(fDoneSem);
			}
			return;
		}
	}

	BWindow::MessageReceived(message);
}


status_t
PasswordDialog::GetPassword(BString& password)
{
	ResizeToPreferred();
	CenterOnScreen();
	Show();

	while (acquire_sem(fDoneSem) == B_INTERRUPTED)
		;

	if (fResult == B_OK)
		password = fPassword;

	LockLooper();
	Quit();
	return fResult;
}
