/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MAIL_ACCOUNT_WINDOW_H
#define _MAIL_ACCOUNT_WINDOW_H


#include <map>
#include <vector>

#include <Messenger.h>
#include <Window.h>

#include <Account.h>


class BButton;
class BCheckBox;
class BGridLayout;
class BMenuField;
class BRadioButton;
class BTextControl;
class BView;


// Editor for a mail account. The account is stored in the roster with the
// fields and key store ids that WebMail reads.
class MailAccountWindow : public BWindow {
public:
								MailAccountWindow(const BMessenger& target,
									const BAccount* account = NULL);

	virtual	void				MessageReceived(BMessage* message);

	static	bool				IsMailAccount(const BAccount& account);
	static	void				DescribeAccount(const BAccount& account,
									BString& text);
	static	void				RemoveSecrets(const char* id);

private:
			BTextControl*		_NewField(const char* label);
			void				_AddTextRow(BTextControl* control, int32 row);
			void				_AddMenuRow(BMenuField* field, int32 row);
			void				_AddServerRow(BTextControl* host,
									BTextControl* port, BCheckBox* tls,
									int32 row);
			void				_ShowRow(int32 row, bool show);
			void				_UpdateKind();
			void				_FillFromProvider(const char* providerId);
			void				_SelectOAuth(const char* oauth);
			void				_UpdateClientId();
			void				_Load(const BAccount& account);
			void				_RunAutoconfig();
			void				_ApplyAutoconfig(const BMessage* message);
			void				_Save();

			BMessenger			fTarget;
			bool				fEditing;
			BAccount			fAccount;

			BGridLayout*		fGrid;
			BRadioButton*		fKindWeb;
			BRadioButton*		fKindImap;
			BRadioButton*		fKindPop3;
			BMenuField*			fProviderField;
			BMenuField*			fOauthProviderField;
			BTextControl*		fAddress;
			BTextControl*		fPassword;
			BTextControl*		fUrl;
			BTextControl*		fIdentityName;
			BTextControl*		fImapHost;
			BTextControl*		fImapPort;
			BTextControl*		fPop3Host;
			BTextControl*		fPop3Port;
			BTextControl*		fSmtpHost;
			BTextControl*		fSmtpPort;
			BCheckBox*			fImapTls;
			BCheckBox*			fPop3Tls;
			BCheckBox*			fSmtpTls;
			BTextControl*		fClientId;
			BButton*			fAutoconfigButton;
			BButton*			fSaveButton;

			std::map<int32, std::vector<BView*> >	fRowViews;
};


#endif // _MAIL_ACCOUNT_WINDOW_H
