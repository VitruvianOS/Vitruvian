/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */
#ifndef PASSWORD_DIALOG_H
#define PASSWORD_DIALOG_H


#include <String.h>
#include <Window.h>


class BButton;
class BStringView;
class BTextControl;


// Modal dialog that changes a user's password via PAM (pam_start("passwd")
// + pam_chauthtok). Takes over the running task while open; posts the
// result back through the supplied BMessenger.
class PasswordDialog : public BWindow {
public:
								PasswordDialog(const char* user);
	virtual						~PasswordDialog();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Submit();
			bool				_ChangePassword(const char* oldPass,
									const char* newPass, BString& error);

			BString				fUser;
			BTextControl*		fOldPassword;
			BTextControl*		fNewPassword;
			BTextControl*		fNewPasswordConfirm;
			BButton*			fOkButton;
			BStringView*		fStatus;
};


#endif // PASSWORD_DIALOG_H
