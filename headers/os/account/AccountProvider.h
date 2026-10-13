/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNT_PROVIDER_H
#define _ACCOUNT_PROVIDER_H


#include <Messenger.h>
#include <Message.h>
#include <SupportDefs.h>

#include <Account.h>


// Add-ons in /system/add-ons/accounts export
// extern "C" BAccountProvider* instantiate_account_provider().
class BAccountProvider {
public:
	virtual						~BAccountProvider();

	// Stable identifier, also the file name suffix in the roster:
	// "imap-smtp", "oauth2", ...
	virtual	const char*			Id() const = 0;
	virtual	const char*			DisplayName() const = 0;
	virtual	uint32				Capabilities() const = 0;

	// Opens the setup window for account. On save the add-on applies the
	// collected settings to the account, commits it to the roster and
	// posts kAccountEditResult ("status") to replyTo.
	virtual	status_t			EditAccount(BAccount& account,
									const BMessenger& replyTo);

	// Verifies or upgrades the credentials collected by EditAccount.
	virtual	status_t			Authenticate(BAccount& account,
									const BMessage& parameters);

	// Returns an access token in token ("token", "expiry"). consentTo
	// receives kAccountConsentRequest when the user has to sign in.
	virtual	status_t			GetToken(BAccount& account,
									const BMessenger& consentTo,
									BMessage& token);

	virtual	status_t			RemoveAccount(BAccount& account);
};


typedef BAccountProvider* (*instantiate_account_provider_func)();

extern "C" BAccountProvider* instantiate_account_provider();


#endif // _ACCOUNT_PROVIDER_H
