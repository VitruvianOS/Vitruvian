/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _ACCOUNTS_SIP_ACCOUNTS_H
#define _ACCOUNTS_SIP_ACCOUNTS_H


#include <Message.h>
#include <String.h>

#include <vector>


// SIP account provider for the Accounts kit. Settings are flat BMessage
// files under ~/config/settings/Accounts/sip/; the password goes into the
// keystore under the "sip" keyring, keyed by account id.
class BSipAccounts {
public:
	static BSipAccounts*	Default();

								BSipAccounts();
								~BSipAccounts();

			// settings carries the kSipField* keys from SipBackend.h
			// plus "account_name". Password is written to the
			// keystore; on success *idOut receives the account id.
			status_t			AddAccount(const BMessage& settings,
									const char* password,
									BString* idOut = NULL);
			status_t			RemoveAccount(const char* id);

			int32				CountAccounts();
			// Fills settings and returns the password (empty when
			// the keystore has none) for one stored account.
			status_t			AccountAt(int32 index, BMessage* settings,
									BString* password = NULL);
			status_t			FindAccount(const char* id,
									BMessage* settings,
									BString* password = NULL);

			// Loads every stored account into the SIP backend;
			// called by BTelephonyRoster at startup.
			status_t			LoadIntoBackend(void* sipBackend);

private:
			void				Load();
			status_t			_SettingsPath(const char* id,
									BString* path);
			status_t			_ReadPassword(const char* id,
									BString* password);
			status_t			_WritePassword(const char* id,
									const char* password);

			struct Entry {
				BString		id;
				BMessage	settings;
			};

			std::vector<Entry>	fAccounts;
};


#endif // _ACCOUNTS_SIP_ACCOUNTS_H
