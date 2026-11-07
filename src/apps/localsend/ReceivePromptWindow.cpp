/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "ReceivePromptWindow.h"

#include <Button.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Message.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>

#include <Application.h>

#include "LocalSendMessages.h"


static const uint32 kMsgAccept = 'pacc';
static const uint32 kMsgDecline = 'pdec';
static const uint32 kMsgAlways = 'palw';


ReceivePromptWindow::ReceivePromptWindow(BRect frame,
	const BString& senderAlias, const BStringList& files,
	const BMessenger& answerTarget)
	:
	BWindow(frame, "Incoming Files", B_TITLED_WINDOW,
		B_NOT_CLOSABLE | B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE
			| B_MODAL_WINDOW | B_AUTO_UPDATE_SIZE_LIMITS),
	fSenderAlias(senderAlias),
	fAnswerTarget(answerTarget)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BString messageText(senderAlias);
	messageText << " wants to send you " << files.CountStrings()
		<< " file(s):";
	BStringView* label = new BStringView("label", messageText.String());

	BTextView* fileList = new BTextView("files");
	fileList->MakeEditable(false);
	fileList->MakeSelectable(true);
	BString fileText;
	for (int32 i = 0; i < files.CountStrings(); i++) {
		fileText << files.StringAt(i) << "\n";
	}
	fileList->SetText(fileText.String());
	BScrollView* scroll = new BScrollView("scroll", fileList, 0, true, true);

	BButton* accept = new BButton("Accept", new BMessage(kMsgAccept));
	accept->SetTarget(this);
	BButton* always = new BButton("Always Accept",
		new BMessage(kMsgAlways));
	always->SetTarget(this);
	BButton* decline = new BButton("Decline", new BMessage(kMsgDecline));
	decline->SetTarget(this);

	BLayoutBuilder::Group<>(top, B_VERTICAL, 8)
		.Add(label)
		.Add(scroll, 1.0)
		.AddGroup(B_HORIZONTAL, 10)
			.Add(accept)
			.Add(always)
			.Add(decline)
		.End()
		.SetInsets(10, 10, 10, 10);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(top);

	// Show without stealing focus aggressively; the user may be
	// typing elsewhere.
	SetWorkspaces(Workspaces() | 1 << 1);
}


bool
ReceivePromptWindow::QuitRequested()
{
	// Closing the prompt without answering counts as a decline so
	// the sender is not left hanging until the timeout.
	_Answer(false, false);
	return true;
}


void
ReceivePromptWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAccept:
			_Answer(true, false);
			break;

		case kMsgAlways:
			_Answer(true, true);
			break;

		case kMsgDecline:
			_Answer(false, false);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ReceivePromptWindow::_Answer(bool accept, bool always)
{
	// Forward to the main window, whose handlers talk to the manager.
	// The message constants match the window's prompt-answer codes.
	uint32 what = kMsgDecline;
	if (accept)
		what = always ? 'palw' : 'pacc';

	BMessage answer(what);
	answer.AddBool("always", always);
	fAnswerTarget.SendMessage(&answer);
	PostMessage(B_QUIT_REQUESTED);
}
