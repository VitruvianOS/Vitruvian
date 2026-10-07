/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SecretDialogWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiSecretDialog"


static const uint32 kMsgConnect = 'sdcn';
static const uint32 kMsgCancel = 'sdcl';
static const uint32 kMsgToggleShow = 'sdsh';


SecretDialogWindow::SecretDialogWindow(secret_dialog_kind kind,
	const BMessage& request, const BMessenger& replyTarget, uint32 replyWhat)
	:
	BWindow(BRect(0, 0, 320, 10), B_TRANSLATE("Network Password"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
	fKind(kind),
	fReplyTarget(replyTarget),
	fReplyWhat(replyWhat),
	fRequestId(0),
	fResultSent(false),
	fPasswordField(NULL),
	fIdentityField(NULL),
	fShowPassword(NULL),
	fRemember(NULL)
{
	request.FindUInt32("request_id", &fRequestId);

	BString ssid;
	request.FindString("ssid", &ssid);
	bool requestNew = false;
	request.FindBool("request_new", &requestNew);

	switch (kind) {
		case kSecretWPAPSK:
			_BuildPSK(ssid, requestNew, false);
			break;
		case kSecretWEP:
			_BuildPSK(ssid, requestNew, true);
			break;
		case kSecretEnterprise:
		{
			BString method;
			request.FindString("method", &method);
			_BuildEnterprise(ssid, method, false);
			break;
		}
		case kSecretWired8021x:
		{
			BString method;
			request.FindString("method", &method);
			_BuildEnterprise(BString(), method, true);
			break;
		}
		case kSecretMissingCertificate:
		{
			BString missingFile;
			request.FindString("missing_file", &missingFile);
			_BuildMissingCertificate(missingFile);
			break;
		}
		case kSecretVPN:
		case kSecretWireGuard:
			_BuildVPN(request);
			break;
	}

	CenterOnScreen();
}


SecretDialogWindow::~SecretDialogWindow()
{
}


void
SecretDialogWindow::_BuildPSK(const BString& ssid, bool requestNew, bool isWEP)
{
	BString heading;
	if (requestNew) {
		// The easy-to-miss case: a stored key was rejected by the AP, this
		// is not a first connect. Must not look identical to the blank
		// first-connect prompt below.
		heading.SetToFormat(
			B_TRANSLATE("The password for \"%s\" is incorrect."), ssid.String());
	} else {
		heading.SetToFormat(B_TRANSLATE("Password required for \"%s\""),
			ssid.String());
	}

	BStringView* prompt = new BStringView(NULL, heading.String());

	fPasswordField = new BTextControl(NULL, B_TRANSLATE("Password:"), NULL,
		NULL);
	fPasswordField->TextView()->HideTyping(true);

	fShowPassword = new BCheckBox(B_TRANSLATE("Show password"),
		new BMessage(kMsgToggleShow));

	fRemember = new BCheckBox(B_TRANSLATE("Remember this network"), NULL);
	fRemember->SetValue(B_CONTROL_ON);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));
	BButton* connect = new BButton(B_TRANSLATE("Connect"), new BMessage(kMsgConnect));
	connect->MakeDefault(true);

	BLayoutBuilder::Group<> builder(this, B_VERTICAL, B_USE_DEFAULT_SPACING);
	builder.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(fPasswordField);

	if (isWEP) {
		builder.Add(new BStringView(NULL,
			B_TRANSLATE("Using key index 0 (the only index this shell "
				"demonstrates).")));
	}

	builder.Add(fShowPassword)
		.Add(fRemember)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(connect)
		.End();

	SetDefaultButton(connect);
	fPasswordField->MakeFocus(true);
}


void
SecretDialogWindow::_BuildEnterprise(const BString& ssid,
	const BString& method, bool wired)
{
	BString heading;
	if (wired) {
		heading = B_TRANSLATE("Authentication required for wired network");
	} else {
		heading.SetToFormat(B_TRANSLATE("Password required for \"%s\""),
			ssid.String());
	}

	BStringView* prompt = new BStringView(NULL, heading.String());

	BString methodLine;
	methodLine.SetToFormat(B_TRANSLATE("Authentication method: %s"),
		method.IsEmpty() ? B_TRANSLATE("PEAP") : method.String());
	BStringView* methodView = new BStringView(NULL, methodLine.String());

	fIdentityField = new BTextControl(NULL, B_TRANSLATE("Identity:"), NULL,
		NULL);
	fPasswordField = new BTextControl(NULL, B_TRANSLATE("Password:"), NULL,
		NULL);
	fPasswordField->TextView()->HideTyping(true);

	fShowPassword = new BCheckBox(B_TRANSLATE("Show password"),
		new BMessage(kMsgToggleShow));

	fRemember = new BCheckBox(B_TRANSLATE("Remember this network"), NULL);
	fRemember->SetValue(B_CONTROL_ON);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));
	BButton* connect = new BButton(B_TRANSLATE("Connect"), new BMessage(kMsgConnect));
	connect->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.Add(methodView)
		.Add(fIdentityField)
		.Add(fPasswordField)
		.Add(fShowPassword)
		.Add(fRemember)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(connect)
		.End();

	SetDefaultButton(connect);
	fIdentityField->MakeFocus(true);
}


static const char*
_SecretLabel(const BString& key)
{
	if (key == "password")
		return B_TRANSLATE("Password:");
	if (key == "private-key")
		return B_TRANSLATE("Private key:");
	if (key == "preshared-key")
		return B_TRANSLATE("Pre-shared key:");
	if (key == "cert-pass")
		return B_TRANSLATE("Certificate password:");
	if (key == "Xauth password" || key == "xauth-password")
		return B_TRANSLATE("XAuth password:");
	if (key == "IPSec secret" || key == "group-password")
		return B_TRANSLATE("Group password:");
	if (key == "http-proxy-password")
		return B_TRANSLATE("Proxy password:");
	return NULL;
}


void
SecretDialogWindow::_BuildVPN(const BMessage& request)
{
	BString name;
	request.FindString("connection_name", &name);
	BString heading;
	if (name.IsEmpty())
		heading = B_TRANSLATE("The VPN connection needs authentication.");
	else {
		heading.SetToFormat(B_TRANSLATE("\"%s\" needs authentication."),
			name.String());
	}

	BLayoutBuilder::Group<> builder(this, B_VERTICAL, B_USE_DEFAULT_SPACING);
	builder.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView(NULL, heading.String()));

	BMessage messages;
	request.FindMessage("secret_messages", &messages);
	BString text;
	for (int32 i = 0; messages.FindString("message", i, &text) == B_OK; i++)
		builder.Add(new BStringView(NULL, text.String()));

	BMessage keys;
	request.FindMessage("secret_keys", &keys);
	BString key;
	for (int32 i = 0; keys.FindString("key", i, &key) == B_OK; i++) {
		if (key.IsEmpty())
			continue;
		const char* label = _SecretLabel(key);
		BString fallback;
		if (label == NULL) {
			fallback.SetToFormat("%s:", key.String());
			label = fallback.String();
		}
		BTextControl* control = new BTextControl(NULL, label, NULL, NULL);
		control->TextView()->HideTyping(true);
		fSecretFields.AddItem(new SecretField{key, control});
		builder.Add(control);
	}

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));
	BButton* connect = new BButton(B_TRANSLATE("Connect"),
		new BMessage(kMsgConnect));

	builder.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(connect)
		.End();

	SetDefaultButton(connect);
	if (!fSecretFields.IsEmpty())
		fSecretFields.ItemAt(0)->control->MakeFocus(true);
}


void
SecretDialogWindow::_BuildMissingCertificate(const BString& missingFile)
{
	BString text;
	text.SetToFormat(
		B_TRANSLATE("This network needs a certificate that is not "
			"installed:\n%s"),
		missingFile.IsEmpty()
			? B_TRANSLATE("(unknown file)") : missingFile.String());

	BStringView* prompt = new BStringView(NULL, text.String());
	prompt->SetExplicitMaxSize(BSize(280 * be_plain_font->Size() / 12.0f,
		B_SIZE_UNSET));

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));
	cancel->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(prompt)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
		.End();

	SetDefaultButton(cancel);
}


void
SecretDialogWindow::_ToggleShowPassword()
{
	if (fPasswordField == NULL || fShowPassword == NULL)
		return;
	fPasswordField->TextView()->HideTyping(
		fShowPassword->Value() != B_CONTROL_ON);
}


void
SecretDialogWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleShow:
			_ToggleShowPassword();
			break;

		case kMsgConnect:
			_SendResult(true);
			PostMessage(B_QUIT_REQUESTED);
			break;

		case kMsgCancel:
			_SendResult(false);
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SecretDialogWindow::QuitRequested()
{
	_SendResult(false);
	return true;
}


void
SecretDialogWindow::_SendResult(bool connect)
{
	if (fResultSent)
		return;
	fResultSent = true;

	BMessage result(fReplyWhat);
	result.AddInt32("kind", (int32)fKind);
	result.AddUInt32("request_id", fRequestId);
	result.AddBool("connect", connect);
	if (connect) {
		if (!fSecretFields.IsEmpty()) {
			for (int32 i = 0; i < fSecretFields.CountItems(); i++) {
				SecretField* field = fSecretFields.ItemAt(i);
				result.AddString("secret_key", field->key);
				result.AddString("secret_value", field->control->Text());
			}
		} else {
			if (fPasswordField != NULL)
				result.AddString("password", fPasswordField->Text());
			if (fIdentityField != NULL)
				result.AddString("identity", fIdentityField->Text());
		}
		result.AddBool("remember",
			fRemember != NULL && fRemember->Value() == B_CONTROL_ON);
	}
	fReplyTarget.SendMessage(&result);
}
