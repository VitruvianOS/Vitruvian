/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */


#include "ResizeMoveWindow.h"

#include <stdlib.h>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ResizeMoveWindow"


enum {
	kMsgOk				= 'rmok',
	kMsgSizeSlider		= 'rmss',
	kMsgSizeText		= 'rmst',
	kMsgStartSlider		= 'rmls',
	kMsgStartText		= 'rmlt'
};


class ResizeMoveWindow::EscapeFilter : public BMessageFilter {
public:
	EscapeFilter(ResizeMoveWindow* target)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		fWindow(target)
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
				fWindow->Cancel();
				return B_SKIP_MESSAGE;
			}
		}
		return B_DISPATCH_MESSAGE;
	}

private:
	ResizeMoveWindow*	fWindow;
};


ResizeMoveWindow::ResizeMoveWindow(BWindow* window, const char* title,
	off_t currentSizeMiB, off_t minSizeMiB, off_t maxSizeMiB, bool canResize,
	off_t currentStartMiB, off_t minStartMiB, off_t maxStartMiB,
	bool canMove, off_t usedMiB)
	:
	BWindow(BRect(300.0, 200.0, 620.0, 340.0), title, B_MODAL_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSizeSlider(NULL),
	fSizeTextControl(NULL),
	fStartSlider(NULL),
	fStartTextControl(NULL),
	fOkButton(new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgOk))),
	fMinSizeMiB(minSizeMiB),
	fMaxSizeMiB(maxSizeMiB),
	fMinStartMiB(minStartMiB),
	fMaxStartMiB(maxStartMiB),
	fCurrentSizeMiB(currentSizeMiB),
	fCurrentStartMiB(currentStartMiB),
	fCanResize(canResize),
	fCanMove(canMove),
	fReturnStatus(B_CANCELED),
	fExitSemaphore(create_sem(0, "ResizeMoveWindow exit")),
	fWindow(window)
{
	AddCommonFilter(new EscapeFilter(this));
	AddToSubset(fWindow);

	BLayoutBuilder::Group<> builder(this, B_VERTICAL);
	builder.SetInsets(B_USE_DEFAULT_SPACING);

	// int32 MiB sliders overflow only past 2 PiB; bounds are the caller's job.
	if (fCanResize) {
		fSizeSlider = new BSlider("size slider", B_TRANSLATE("New size"),
			new BMessage(kMsgSizeSlider), (int32)fMinSizeMiB,
			(int32)fMaxSizeMiB, B_HORIZONTAL);
		fSizeSlider->SetValue((int32)fCurrentSizeMiB);

		fSizeTextControl = new BTextControl("size text", "", "", NULL);
		for (int32 i = 0; i < 256; i++)
			fSizeTextControl->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fSizeTextControl->TextView()->AllowChar(i);
		fSizeTextControl->SetModificationMessage(new BMessage(kMsgSizeText));
		_UpdateSizeTextControl();

		builder.Add(fSizeSlider).Add(fSizeTextControl);

		// usedMiB < 0 means the helper couldn't tell; only shown when known.
		if (usedMiB >= 0) {
			BString usedLabel(B_TRANSLATE("Currently using %usedMiB% MiB"));
			BString usedStr;
			usedStr << usedMiB;
			usedLabel.ReplaceFirst("%usedMiB%", usedStr);
			builder.Add(new BStringView("used label", usedLabel.String()));
		}
	} else {
		BString label(B_TRANSLATE("Size: %sizeMiB% MiB (this filesystem "
			"cannot be resized)"));
		BString sizeStr;
		sizeStr << fCurrentSizeMiB;
		label.ReplaceFirst("%sizeMiB%", sizeStr);
		builder.Add(new BStringView("size disabled", label.String()));
	}

	if (fCanMove) {
		fStartSlider = new BSlider("start slider",
			B_TRANSLATE("New start offset"), new BMessage(kMsgStartSlider),
			(int32)fMinStartMiB, (int32)fMaxStartMiB, B_HORIZONTAL);
		fStartSlider->SetValue((int32)fCurrentStartMiB);

		fStartTextControl = new BTextControl("start text", "", "", NULL);
		for (int32 i = 0; i < 256; i++)
			fStartTextControl->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fStartTextControl->TextView()->AllowChar(i);
		fStartTextControl->SetModificationMessage(
			new BMessage(kMsgStartText));
		_UpdateStartTextControl();

		builder.Add(fStartSlider).Add(fStartTextControl);
	}

	builder.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(new BButton(B_TRANSLATE("Cancel"), new BMessage(B_CANCEL)))
			.Add(fOkButton)
		.End();

	SetDefaultButton(fOkButton);
}


ResizeMoveWindow::~ResizeMoveWindow()
{
	delete_sem(fExitSemaphore);
}


bool
ResizeMoveWindow::QuitRequested()
{
	release_sem(fExitSemaphore);
	return false;
}


void
ResizeMoveWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CANCEL:
			Cancel();
			break;

		case kMsgOk:
			fReturnStatus = B_OK;
			release_sem(fExitSemaphore);
			break;

		case kMsgSizeSlider:
			_UpdateSizeTextControl();
			break;

		case kMsgSizeText:
		{
			off_t size = strtoll(fSizeTextControl->Text(), NULL, 10);
			if (size >= fMinSizeMiB && size <= fMaxSizeMiB)
				fSizeSlider->SetValue((int32)size);
			else
				_UpdateSizeTextControl();
			break;
		}

		case kMsgStartSlider:
			_UpdateStartTextControl();
			break;

		case kMsgStartText:
		{
			off_t start = strtoll(fStartTextControl->Text(), NULL, 10);
			if (start >= fMinStartMiB && start <= fMaxStartMiB)
				fStartSlider->SetValue((int32)start);
			else
				_UpdateStartTextControl();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


void
ResizeMoveWindow::Cancel()
{
	fReturnStatus = B_CANCELED;
	release_sem(fExitSemaphore);
}


void
ResizeMoveWindow::_UpdateSizeTextControl()
{
	BString sizeString;
	sizeString << fSizeSlider->Value();
	fSizeTextControl->SetText(sizeString.String());
}


void
ResizeMoveWindow::_UpdateStartTextControl()
{
	BString startString;
	startString << fStartSlider->Value();
	fStartTextControl->SetText(startString.String());
}


status_t
ResizeMoveWindow::Go(off_t& newSizeMiB, off_t& newStartMiB)
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
		newSizeMiB = fCanResize ? (off_t)fSizeSlider->Value()
			: fCurrentSizeMiB;
		newStartMiB = fCanMove ? (off_t)fStartSlider->Value()
			: fCurrentStartMiB;
	}

	status_t status = fReturnStatus;

	Quit();
		// NOTE: this object is toast now!

	return status;
}
