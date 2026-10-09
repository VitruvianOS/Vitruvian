/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <ObexReceivePrompt.h>
#include <ObexClient.h>

#include <BluetoothSettings.h>

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth receive"


static const uint32 kMsgAccept = 'orac';
static const uint32 kMsgReject = 'orre';


static BString
_FormatSize(uint64 size)
{
	BString text;
	if (size >= 1024ULL * 1024ULL) {
		text << (size / (1024.0 * 1024.0)) << " MB";
	} else if (size >= 1024ULL) {
		text << (size / 1024.0) << " KB";
	} else {
		text << size << B_TRANSLATE(" bytes");
	}
	return text;
}


ObexReceivePrompt::ObexReceivePrompt(uint32 requestId, const BString& sender,
	const BString& fileName, uint64 fileSize, bool canAlwaysAccept,
	BluetoothSettings* settings, const char* senderAddress)
	:
	BWindow(BRect(80, 80, 400, 220), B_TRANSLATE("Incoming file"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fRequestId(requestId),
	fSettings(settings),
	fSenderAddress(senderAddress != NULL ? senderAddress : "")
{
	BString detail;
	detail << B_TRANSLATE("Send from:") << " " << sender << "\n"
		<< B_TRANSLATE("File:") << " " << fileName << "\n"
		<< B_TRANSLATE("Size:") << " " << _FormatSize(fileSize);

	fDetailView = new BStringView("detail", detail.String());

	fAlwaysCheckBox = new BCheckBox("always",
		B_TRANSLATE("Always accept files from this device"),
		new BMessage((uint32)0));
	fAlwaysCheckBox->SetEnabled(canAlwaysAccept);
	fAlwaysCheckBox->SetValue(B_CONTROL_OFF);

	fAcceptButton = new BButton("accept", B_TRANSLATE("Accept"),
		new BMessage(kMsgAccept));
	fAcceptButton->MakeDefault(true);

	fRejectButton = new BButton("reject", B_TRANSLATE("Reject"),
		new BMessage(kMsgReject));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fDetailView)
		.Add(fAlwaysCheckBox)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fRejectButton)
			.Add(fAcceptButton)
		.End()
	.End();

	CenterOnScreen();

	fAcceptButton->SetTarget(this);
	fRejectButton->SetTarget(this);
}


ObexReceivePrompt::~ObexReceivePrompt()
{
}


void
ObexReceivePrompt::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAccept:
		{
			if (fSettings != NULL && !fSenderAddress.IsEmpty()
					&& fAlwaysCheckBox->Value() == B_CONTROL_ON) {
				fSettings->SetAlwaysAccept(fSenderAddress, true);
				fSettings->SaveSettings();
			}
			ObexClient::CompleteRequest(fRequestId, true);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case kMsgReject:
			ObexClient::CompleteRequest(fRequestId, false);
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
