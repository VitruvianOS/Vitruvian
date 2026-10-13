/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_PROVIDERS_H
#define WEB_MAIL_PROVIDERS_H


#include <String.h>
#include <vector>


// One webmail provider: where its inbox lives, where its compose
// window lives and which OAuth2 endpoints its native IMAP sibling
// (if any) would use. "Custom" stores the user's own URL.
struct WebProvider {
	const char*	id;
	const char*	name;
	const char*	url;
	// Compose URL; "%s" is replaced with the percent-encoded
	// recipient address. Empty when the provider has none.
	const char*	composeUrl;
	// OAuth2 provider key for native IMAP/SMTP accounts on this
	// service ("google", "microsoft", "yahoo"); NULL when the
	// service has no OAuth2 for mail.
	const char*	oauthProvider;
	// Extra JavaScript injected into the provider's pages; posts
	// {"type":"badge","count":N} through the webmail script message
	// handler when the page exposes a DOM unread badge. NULL when
	// the title alone is enough.
	const char*	badgeScript;
};


// The catalog, terminated by a NULL id. Never freed.
const WebProvider*	FindWebProvider(const char* id);
void				GetWebProviders(std::vector<WebProvider>& providers);

// Builds the compose URL for a provider and address; returns false
// when the provider has no compose URL.
bool				ComposeURLFor(const WebProvider* provider,
							const char* address, BString& url);

// Percent-encodes a string for a URL query component.
BString				URLEncode(const char* text);


#endif // WEB_MAIL_PROVIDERS_H
