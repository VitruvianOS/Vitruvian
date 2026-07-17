/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */

#include "PasswordDialog.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include <Box.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>


static const uint32 kMsgOk = 'okok';
static const uint32 kMsgCancel = 'cncl';


struct PamPassContext {
	const char* oldPass;
	const char* newPass;
	int prompt;
};


// pam_chauthtok issues two PROMPT_ECHO_OFF messages: the first for the
// current authentication token, the second for the new one. Anything
// else (info/error) we return an empty response for.
static int
password_conv(int num, const struct pam_message** msg,
	struct pam_response** resp, void* data)
{
	PamPassContext* ctx = (PamPassContext*)data;
	if (num <= 0 || msg == NULL || resp == NULL)
		return PAM_CONV_ERR;

	struct pam_response* r = (struct pam_response*)calloc(num,
		sizeof(struct pam_response));
	if (r == NULL)
		return PAM_BUF_ERR;

	for (int i = 0; i < num; i++) {
		const char* answer = "";
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			answer = (ctx->prompt == 0) ? ctx->oldPass : ctx->newPass;
			ctx->prompt++;
		}
		r[i].resp = strdup(answer);
		if (r[i].resp == NULL) {
			for (int j = 0; j < i; j++)
				free(r[j].resp);
			free(r);
			return PAM_BUF_ERR;
		}
	}
	*resp = r;
	return PAM_SUCCESS;
}


PasswordDialog::PasswordDialog(const char* user)
	:
	BWindow(BRect(0, 0, 380, 200), "Change password",
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fUser(user)
{
	fOldPassword = new BTextControl("old", "Current password:", "", NULL);
	fOldPassword->TextView()->HideTyping(true);

	fNewPassword = new BTextControl("new", "New password:", "", NULL);
	fNewPassword->TextView()->HideTyping(true);

	fNewPasswordConfirm = new BTextControl("confirm", "Confirm:", "", NULL);
	fNewPasswordConfirm->TextView()->HideTyping(true);

	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	fOkButton = new BButton("ok", "Change", new BMessage(kMsgOk));
	fOkButton->MakeDefault(true);
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(kMsgCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fOldPassword)
		.Add(fNewPassword)
		.Add(fNewPasswordConfirm)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fOkButton)
		.End();

	CenterOnScreen();
	fOldPassword->MakeFocus(true);
	Show();
}


PasswordDialog::~PasswordDialog()
{
}


void
PasswordDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOk:
			_Submit();
			break;
		case kMsgCancel:
			PostMessage(B_QUIT_REQUESTED);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
PasswordDialog::_Submit()
{
	BString oldPass(fOldPassword->Text());
	BString newPass(fNewPassword->Text());
	BString confirm(fNewPasswordConfirm->Text());

	if (newPass.Length() == 0) {
		fStatus->SetText("New password must not be empty.");
		return;
	}
	if (newPass != confirm) {
		fStatus->SetText("New passwords do not match.");
		fNewPassword->SetText("");
		fNewPasswordConfirm->SetText("");
		fNewPassword->MakeFocus(true);
		return;
	}

	BString error;
	bool ok = _ChangePassword(oldPass.String(), newPass.String(), error);

	// Wipe our copies immediately either way.
	explicit_bzero(oldPass.LockBuffer(0), oldPass.Length());
	oldPass.UnlockBuffer(0);
	explicit_bzero(newPass.LockBuffer(0), newPass.Length());
	newPass.UnlockBuffer(0);
	explicit_bzero(confirm.LockBuffer(0), confirm.Length());
	confirm.UnlockBuffer(0);
	fOldPassword->SetText("");
	fNewPassword->SetText("");
	fNewPasswordConfirm->SetText("");

	if (ok) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	fStatus->SetText(error.Length() > 0 ? error.String()
		: "Password change failed.");
	fOldPassword->MakeFocus(true);
}


bool
PasswordDialog::_ChangePassword(const char* oldPass, const char* newPass,
	BString& error)
{
	PamPassContext ctx = { oldPass, newPass, 0 };
	struct pam_conv conv = { password_conv, &ctx };
	pam_handle_t* h = NULL;
	int r = pam_start("passwd", fUser.String(), &conv, &h);
	if (r != PAM_SUCCESS) {
		error = "PAM start failed";
		return false;
	}
	r = pam_chauthtok(h, 0);
	if (r != PAM_SUCCESS)
		error = pam_strerror(h, r);
	pam_end(h, r);
	return r == PAM_SUCCESS;
}
