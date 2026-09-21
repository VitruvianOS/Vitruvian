/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */


#include "FlagsPanel.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <Partition.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FlagsPanel"


static const uint32 kMsgOk = 'flok';


class FlagsPanel::EscapeFilter : public BMessageFilter {
public:
	EscapeFilter(FlagsPanel* target)
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
	FlagsPanel*	fPanel;
};


FlagsPanel::FlagsPanel(BWindow* window, BPartition* partition)
	:
	BWindow(BRect(300.0, 200.0, 600.0, 300.0), B_TRANSLATE("Partition flags"),
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fOkButton(new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgOk))),
	fReturnStatus(B_CANCELED),
	fExitSemaphore(create_sem(0, "FlagsPanel exit")),
	fWindow(window)
{
	// esp is GPT-only (it retypes the GUID); legacy boot also applies on MBR.
	bool isGPT = true;
	if (BPartition* parent = partition->Parent())
		isGPT = BString(parent->ContentType()) == "gpt";

	fEspCheckBox = new BCheckBox("esp", B_TRANSLATE("EFI System Partition"),
		NULL);
	fEspCheckBox->SetEnabled(isGPT);
	fLegacyBootCheckBox = new BCheckBox("boot", B_TRANSLATE("Legacy boot"),
		NULL);

	AddCommonFilter(new EscapeFilter(this));
	AddToSubset(fWindow);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fEspCheckBox)
		.Add(fLegacyBootCheckBox)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(new BButton(B_TRANSLATE("Cancel"), new BMessage(B_CANCEL)))
			.Add(fOkButton)
		.End();

	SetDefaultButton(fOkButton);
}


FlagsPanel::~FlagsPanel()
{
	delete_sem(fExitSemaphore);
}


bool
FlagsPanel::QuitRequested()
{
	release_sem(fExitSemaphore);
	return false;
}


void
FlagsPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CANCEL:
			Cancel();
			break;

		case kMsgOk:
			fReturnStatus = B_OK;
			release_sem(fExitSemaphore);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
FlagsPanel::Cancel()
{
	fReturnStatus = B_CANCELED;
	release_sem(fExitSemaphore);
}


status_t
FlagsPanel::Go(BMessage& flags)
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

	if (fReturnStatus == B_OK) {
		if (fEspCheckBox->Value() == B_CONTROL_ON)
			flags.AddString("flag", "esp");
		if (fLegacyBootCheckBox->Value() == B_CONTROL_ON)
			flags.AddString("flag", "boot");
	}

	status_t status = fReturnStatus;

	Quit();
		// NOTE: this object is toast now!

	return status;
}
