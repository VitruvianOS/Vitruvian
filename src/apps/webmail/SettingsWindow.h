/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_SETTINGS_WINDOW_H
#define WEB_MAIL_SETTINGS_WINDOW_H


#include "Accounts.h"

#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BTextControl;


// Per-account WebMail settings: signature, IMAP IDLE, POP3 retention and
// the OAuth2 sign-in. Server and credential fields live in Accounts.
class SettingsWindow : public BWindow {
public:
						SettingsWindow(const Account& account,
							const BMessenger& target);

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

			const char*	AccountId() const { return fAccountId.String(); }

private:
			void		_Save();
			void		_SignIn();

	BString				fAccountId;
	BString				fOAuthProvider;
	BMessenger			fTarget;
	BTextControl*		fSignature;
	BCheckBox*			fUseIdle;
	BCheckBox*			fLeaveOnServer;
	BTextControl*		fDeleteAfterDays;
	BTextControl*		fClientId;
	BButton*			fOAuthButton;
};


#endif // WEB_MAIL_SETTINGS_WINDOW_H
