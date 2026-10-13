/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _IMAP_PROVIDER_H
#define _IMAP_PROVIDER_H


#include <AccountProvider.h>


class ImapProvider : public BAccountProvider {
public:
	virtual	const char*			Id() const;
	virtual	const char*			DisplayName() const;
	virtual	uint32				Capabilities() const;

	virtual	status_t			EditAccount(BAccount& account,
									const BMessenger& replyTo);
	virtual	status_t			Authenticate(BAccount& account,
									const BMessage& parameters);
	virtual	status_t			RemoveAccount(BAccount& account);
};


#endif // _IMAP_PROVIDER_H
