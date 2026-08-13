/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SECRET_DIALOG_WINDOW_H
#define SECRET_DIALOG_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BStringView;
class BTextControl;


enum secret_dialog_kind {
	kSecretWPAPSK = 0,
	kSecretWEP,
	kSecretEnterprise,
	kSecretWired8021x,
	kSecretMissingCertificate
};


class SecretDialogWindow : public BWindow {
public:
								SecretDialogWindow(secret_dialog_kind kind,
									const BMessage& request,
									const BMessenger& replyTarget,
									uint32 replyWhat);
	virtual						~SecretDialogWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildPSK(const BString& ssid, bool requestNew,
									bool isWEP);
			void				_BuildEnterprise(const BString& ssid,
									const BString& method, bool wired);
			void				_BuildMissingCertificate(
									const BString& missingFile);
			void				_ToggleShowPassword();
			void				_SendResult(bool connect);

			secret_dialog_kind	fKind;
			BMessenger			fReplyTarget;
			uint32				fReplyWhat;
			uint32				fRequestId;
			bool				fResultSent;
			BTextControl*		fPasswordField;
			BTextControl*		fIdentityField;
			BCheckBox*			fShowPassword;
			BCheckBox*			fRemember;
};


#endif	// SECRET_DIALOG_WINDOW_H
