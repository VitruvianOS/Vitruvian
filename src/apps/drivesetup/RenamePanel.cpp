/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */


#include "RenamePanel.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <String.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RenamePanel"


static const uint32 kMsgOk = 'rnok';


class RenamePanel::EscapeFilter : public BMessageFilter {
public:
	EscapeFilter(RenamePanel* target)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		fPanel(target)
	{
	}

	virtual ~EscapeFilter()
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (message->what == B_KEY_DOWN
			|| message->what == B_UNMAPPED_KEY_DOWN) {
			uint32 key;
			if (message->FindInt32("raw_char", (int32*)&key) >= B_OK
				&& key == B_ESCAPE) {
				fPanel->Cancel();
				return B_SKIP_MESSAGE;
			}
		}
		return B_DISPATCH_MESSAGE;
	}

private:
	RenamePanel*	fPanel;
};


RenamePanel::RenamePanel(BWindow* window, const char* title,
	const char* label, const char* initialValue)
	:
	BWindow(BRect(300.0, 200.0, 600.0, 300.0), title,
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fOkButton(new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgOk))),
	fReturnStatus(B_CANCELED),
	fExitSemaphore(create_sem(0, "RenamePanel exit")),
	fWindow(window)
{
	fTextControl = new BTextControl("value", label, initialValue,
		new BMessage(kMsgOk));

	AddCommonFilter(new EscapeFilter(this));
	AddToSubset(fWindow);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fTextControl)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(new BButton(B_TRANSLATE("Cancel"), new BMessage(B_CANCEL)))
			.Add(fOkButton)
		.End();

	SetDefaultButton(fOkButton);
	fTextControl->MakeFocus(true);
}


RenamePanel::~RenamePanel()
{
	delete_sem(fExitSemaphore);
}


bool
RenamePanel::QuitRequested()
{
	release_sem(fExitSemaphore);
	return false;
}


void
RenamePanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CANCEL:
			Cancel();
			break;

		case kMsgOk:
			// May arrive more than once; extra release_sem calls are harmless.
			fReturnStatus = B_OK;
			release_sem(fExitSemaphore);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
RenamePanel::Cancel()
{
	fReturnStatus = B_CANCELED;
	release_sem(fExitSemaphore);
}


status_t
RenamePanel::Go(BString& value)
{
	Hide();
	Show();
	if (!Lock())
		return B_CANCELED;

	CenterIn(fWindow->Frame());
	Show();
	Unlock();

	while (true) {
		status_t status = acquire_sem_etc(fExitSemaphore, 1,
			B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, 50000);
		if (status != B_TIMED_OUT && status != B_INTERRUPTED)
			break;
		fWindow->UpdateIfNeeded();
	}

	if (!Lock())
		return B_CANCELED;

	if (fReturnStatus == B_OK)
		value = fTextControl->Text();

	status_t status = fReturnStatus;

	Quit();
		// NOTE: this object is toast now!

	return status;
}
