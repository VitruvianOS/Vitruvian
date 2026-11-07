/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OAUTH2_PROVIDER_H
#define _OAUTH2_PROVIDER_H


#include <AccountProvider.h>


class Oauth2Provider : public BAccountProvider {
public:
	virtual	const char*			Id() const;
	virtual	const char*			DisplayName() const;
	virtual	uint32				Capabilities() const;

	virtual	status_t			EditAccount(BAccount& account,
									const BMessenger& replyTo);
	virtual	status_t			GetToken(BAccount& account,
									const BMessenger& consentTo,
									BMessage& token);
	virtual	status_t			RemoveAccount(BAccount& account);
};


#endif // _OAUTH2_PROVIDER_H
