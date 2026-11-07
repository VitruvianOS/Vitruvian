/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PASSWORD_DIALOG_H
#define _PASSWORD_DIALOG_H


#include <String.h>
#include <Window.h>


class PasswordDialog : public BWindow {
public:
								PasswordDialog(const char* title,
									const char* message, bool confirm);
								~PasswordDialog();

			status_t			GetPassword(BString& password);

private:
	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			class PasswordView*	fView;
			sem_id				fDoneSem;
			status_t			fResult;
			bool				fConfirm;
			BString				fPassword;
};


#endif	/* _PASSWORD_DIALOG_H */
