/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_SETTINGS_WINDOW_H
#define WEB_MAIL_SETTINGS_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>


class BButton;
class BGridLayout;
class BCheckBox;
class BMenuField;
class BRadioButton;
class BTextControl;
class BView;


// Add-account panel: web provider (or custom URL) with a password,
// or a native IMAP/POP3 account filled by hand / autoconfig, with
// the OAuth2 sign-in for Google, Microsoft and Yahoo. Client ids are
// operator-supplied: the window stores what the operator pastes.
class SettingsWindow : public BWindow {
public:
						SettingsWindow(const BMessenger& target);

	virtual void		MessageReceived(BMessage* message);

private:
			BTextControl*	_NewField(const char* label);
			void		_AddTextRow(BTextControl* control, int32 row);
			void		_AddMenuRow(BMenuField* field, int32 row);
			void		_AddServerRow(BTextControl* host, BTextControl* port,
							BCheckBox* tls, int32 row);
			void		_ShowRow(int32 row, bool show);
			void		_UpdateKind();
			void		_Save();
			void		_RunAutoconfig();
			void		_RunOAuth();
			void		_FillFromProvider(const char* providerId);

	BMessenger			fTarget;
	BGridLayout*		fGrid;
	std::vector<BView*>	fRowViews[16];
	BRadioButton*		fKindWeb;
	BRadioButton*		fKindImap;
	BRadioButton*		fKindPop3;
	BMenuField*			fProviderField;
	BMenuField*			fOauthProviderField;
	BTextControl*		fAddress;
	BTextControl*		fPassword;
	BTextControl*		fUrl;
	BTextControl*		fIdentityName;
	BTextControl*		fSignature;
	BTextControl*		fImapHost;
	BTextControl*		fImapPort;
	BTextControl*		fPop3Host;
	BTextControl*		fPop3Port;
	BTextControl*		fSmtpHost;
	BTextControl*		fSmtpPort;
	BCheckBox*			fImapTls;
	BCheckBox*			fPop3Tls;
	BCheckBox*			fSmtpTls;
	BCheckBox*			fUseIdle;
	BTextControl*		fClientId;
	BButton*			fAutoconfigButton;
	BButton*			fOAuthButton;
	BButton*			fSaveButton;
};


#endif // WEB_MAIL_SETTINGS_WINDOW_H
