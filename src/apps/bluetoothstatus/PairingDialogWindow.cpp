/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PairingDialogWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothPairingDialog"


static const uint32 kMsgAccept = 'pdac';
static const uint32 kMsgDeny = 'pddn';


static BString
_PasskeyString(uint32 passkey)
{
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "%06lu", (unsigned long)(passkey % 1000000));
	return BString(buffer);
}


PairingDialogWindow::PairingDialogWindow(pairing_dialog_kind kind,
	const BMessage& request, const BMessenger& replyTarget, uint32 replyWhat)
	:
	BWindow(BRect(0, 0, 320, 10), B_TRANSLATE("Bluetooth Pairing"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
	fKind(kind),
	fReplyTarget(replyTarget),
	fReplyWhat(replyWhat),
	fResultSent(false),
	fEntry(NULL),
	fRequestId(0)
{
	request.FindUInt32("request_id", &fRequestId);

	BString device;
	request.FindString("device_name", &device);
	if (device.IsEmpty())
		device = B_TRANSLATE("the device");

	uint32 passkey = 0;
	request.FindUInt32("passkey", &passkey);

	switch (kind) {
		case kPairingRequestConfirmation:
			_BuildConfirmation(device, passkey);
			break;
		case kPairingRequestPasskey:
			_BuildPasskeyEntry(device);
			break;
		case kPairingDisplayPasskey:
			_BuildPasskeyDisplay(device, passkey);
			break;
		case kPairingRequestPinCode:
			_BuildPinEntry(device);
			break;
		case kPairingDisplayPinCode:
		{
			BString pin;
			request.FindString("pin_code", &pin);
			_BuildPinDisplay(device, pin);
			break;
		}
		case kPairingRequestAuthorization:
			_BuildAuthorization(device, BString(), false);
			break;
		case kPairingAuthorizeService:
		{
			BString service;
			request.FindString("service_name", &service);
			if (service.IsEmpty())
				service = B_TRANSLATE("an unknown service");
			_BuildAuthorization(device, service, true);
			break;
		}
	}

	CenterOnScreen();
}


PairingDialogWindow::~PairingDialogWindow()
{
}


void
PairingDialogWindow::_BuildConfirmation(const BString& device, uint32 passkey)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("Does this code match the one on %s?"),
		device.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	BStringView* code = new BStringView(NULL, _PasskeyString(passkey).String());
	code->SetFont(be_bold_font);
	code->SetFontSize(be_bold_font->Size() * 2.0f);
	code->SetAlignment(B_ALIGN_CENTER);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgDeny));
	BButton* confirm = new BButton(B_TRANSLATE("Confirm"), new BMessage(kMsgAccept));
	confirm->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(code)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(confirm)
		.End();

	SetDefaultButton(confirm);
}


void
PairingDialogWindow::_BuildPasskeyEntry(const BString& device)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("Enter the passkey for %s:"), device.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	fEntry = new BTextControl(NULL, NULL, NULL);
	fEntry->TextView()->SetMaxBytes(6);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgDeny));
	BButton* ok = new BButton(B_TRANSLATE("OK"), new BMessage(kMsgAccept));
	ok->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(fEntry)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();

	SetDefaultButton(ok);
	fEntry->MakeFocus(true);
}


void
PairingDialogWindow::_BuildPasskeyDisplay(const BString& device, uint32 passkey)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("Enter this passkey on %s:"), device.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	BStringView* code = new BStringView(NULL, _PasskeyString(passkey).String());
	code->SetFont(be_bold_font);
	code->SetFontSize(be_bold_font->Size() * 2.0f);
	code->SetAlignment(B_ALIGN_CENTER);

	// Real digits-entered count arrives from Agent1::DisplayPasskey's
	// "entered" parameter as it is invoked repeatedly by bluetoothd; the
	// shell shows a fixed sample so the layout can be reviewed standalone.
	BStringView* progress = new BStringView(NULL,
		B_TRANSLATE("Waiting for the remote device" B_UTF8_ELLIPSIS));

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgDeny));
	cancel->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(code)
		.Add(progress)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
		.End();

	SetDefaultButton(cancel);
}


void
PairingDialogWindow::_BuildPinEntry(const BString& device)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("Enter the PIN code for %s:"), device.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	fEntry = new BTextControl(NULL, NULL, NULL);
	fEntry->TextView()->SetMaxBytes(16);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgDeny));
	BButton* ok = new BButton(B_TRANSLATE("OK"), new BMessage(kMsgAccept));
	ok->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(fEntry)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();

	SetDefaultButton(ok);
	fEntry->MakeFocus(true);
}


void
PairingDialogWindow::_BuildPinDisplay(const BString& device, const BString& pin)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("Enter this PIN code on %s:"), device.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	BStringView* code = new BStringView(NULL,
		pin.IsEmpty() ? "0000" : pin.String());
	code->SetFont(be_bold_font);
	code->SetFontSize(be_bold_font->Size() * 1.6f);
	code->SetAlignment(B_ALIGN_CENTER);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgDeny));
	cancel->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(code)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
		.End();

	SetDefaultButton(cancel);
}


void
PairingDialogWindow::_BuildAuthorization(const BString& device,
	const BString& service, bool isService)
{
	BString text;
	if (isService) {
		text.SetToFormat(B_TRANSLATE("Allow %s to use %s?"), device.String(),
			service.String());
	} else {
		text.SetToFormat(B_TRANSLATE("Allow %s to connect?"), device.String());
	}

	BStringView* prompt = new BStringView(NULL, text.String());

	BButton* deny = new BButton(B_TRANSLATE("Deny"), new BMessage(kMsgDeny));
	BButton* allow = new BButton(B_TRANSLATE("Allow"), new BMessage(kMsgAccept));
	allow->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(deny)
			.Add(allow)
		.End();

	SetDefaultButton(allow);
}


void
PairingDialogWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAccept:
			_SendResult(true, fEntry != NULL ? BString(fEntry->Text()) : BString());
			PostMessage(B_QUIT_REQUESTED);
			break;

		case kMsgDeny:
			_SendResult(false, BString());
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
PairingDialogWindow::QuitRequested()
{
	// Closing via the title bar / Esc is a cancel, same as pressing Deny.
	_SendResult(false, BString());
	return true;
}


void
PairingDialogWindow::_SendResult(bool accepted, const BString& value)
{
	if (fResultSent)
		return;
	fResultSent = true;

	BMessage result(fReplyWhat);
	result.AddInt32("kind", (int32)fKind);
	result.AddBool("accepted", accepted);
	result.AddString("value", value);
	result.AddUInt32("request_id", fRequestId);
	fReplyTarget.SendMessage(&result);
}
