/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */


#include "ProgressWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ProgressWindow"


static const uint32 kMsgClose = 'prgc';


ProgressWindow::ProgressWindow(BWindow* window, const BMessage& result)
	:
	BWindow(BRect(300.0, 200.0, 650.0, 450.0), B_TRANSLATE("Apply results"),
		B_MODAL_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fExitSemaphore(create_sem(0, "ProgressWindow exit")),
	fWindow(window)
{
	fOpsList = new BOutlineListView("ops", B_SINGLE_SELECTION_LIST);

	BMessage ops;
	int32 okCount = 0;
	int32 total = 0;
	if (result.FindMessage("ops", &ops) == B_OK) {
		BMessage op;
		for (int32 i = 0; ops.FindMessage((BString() << i).String(), &op)
				== B_OK; i++) {
			BString id, kind, status, detail;
			op.FindString("id", &id);
			op.FindString("op", &kind);
			op.FindString("status", &status);
			op.FindString("detail", &detail);

			BString label;
			label << "[" << id << "] " << kind << ": " << status;
			if (!detail.IsEmpty())
				label << " (" << detail << ")";
			fOpsList->AddItem(new BStringItem(label.String()));

			total++;
			if (status == "ok")
				okCount++;
		}
	}

	BString overall;
	result.FindString("status", &overall);

	fStatusBar = new BStatusBar("summary", B_TRANSLATE("Operations:"));
	fStatusBar->SetMaxValue(total > 0 ? total : 1);
	fStatusBar->Update(okCount);
	BString trailing;
	trailing << okCount << "/" << total << " ok (" << overall << ")";
	fStatusBar->SetTrailingText(trailing.String());

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fStatusBar)
		.Add(new BScrollView("ops scroll", fOpsList, 0, 0, false, true))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(new BButton(B_TRANSLATE("Close"), new BMessage(kMsgClose)))
		.End();

	AddToSubset(fWindow);
}


ProgressWindow::~ProgressWindow()
{
	delete_sem(fExitSemaphore);
}


bool
ProgressWindow::QuitRequested()
{
	release_sem(fExitSemaphore);
	return false;
}


void
ProgressWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgClose:
			release_sem(fExitSemaphore);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
ProgressWindow::Go()
{
	Hide();
	Show();
	if (!Lock())
		return;

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

	if (Lock())
		Quit();
			// NOTE: this object is toast now!
}
