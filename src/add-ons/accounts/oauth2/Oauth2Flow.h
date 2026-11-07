/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OAUTH2_FLOW_H
#define _OAUTH2_FLOW_H


#include <String.h>

#include <Account.h>
#include <Messenger.h>


// OAuth2 authorization code flow with PKCE (RFC 7636) and a loopback
// redirect. There is no embedded browser view yet, so the consent page
// URL is posted to the caller (kAccountConsentRequest) and the flow
// waits for the redirect on http://127.0.0.1:<port>/.
class Oauth2Flow {
public:
			status_t			RunInteractive(BAccount& account,
									const BMessenger& consentTo,
									BMessage& token);
			status_t			Refresh(BAccount& account, BMessage& token);

			// Token storage in the key store, under the account id.
	static	status_t			Store(const BAccount& account,
									const BString& accessToken,
									const BString& refreshToken,
									bigtime_t expiry);
	static	status_t			Load(const BAccount& account,
									BString& accessToken,
									BString& refreshToken,
									bigtime_t& expiry);
	static	status_t			Remove(const BAccount& account);
};


#endif // _OAUTH2_FLOW_H
