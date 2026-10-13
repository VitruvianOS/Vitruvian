/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_OAUTH2_H
#define WEB_MAIL_OAUTH2_H


#include <String.h>
#include <Messenger.h>

#include <ctime>


struct OAuth2Endpoints {
	BString		authorizeUrl;
	BString		tokenUrl;
	BString		scope;
	// Filled in by the agent: http://127.0.0.1:<port>/
	BString		redirectUri;
};


struct OAuth2Token {
	BString		accessToken;
	BString		refreshToken;
	time_t		expiresAt;		// 0: never expires
};


// Endpoint tables for the OAuth2 providers. Client ids are operator-supplied:
// register an installed application per provider and paste its id into
// Settings.
bool	OAuth2EndpointsFor(const char* provider, OAuth2Endpoints& endpoints);

// Client id currently configured for a provider (Settings).
BString	OAuth2ClientIdFor(const char* provider);
void	OAuth2SetClientIdFor(const char* provider, const char* clientId);

// PKCE: 64 character verifier and its base64url(SHA-256) challenge.
void	OAuth2MakePKCE(BString& verifier, BString& challenge);
// Random URL-safe state string.
BString	OAuth2MakeState();

// Builds the authorization URL the consent view loads.
BString	OAuth2AuthorizationURL(const OAuth2Endpoints& endpoints,
			const char* clientId, const char* state,
			const char* codeChallenge);

// Pure JSON parsing of the token endpoint response (host testable).
bool	ParseOAuth2TokenJson(const char* json, OAuth2Token& token,
			BString& error);

// POST x-www-form-urlencoded over HTTPS; returns false on transport
// failure, response carries the body either way.
bool	OAuth2HttpPost(const char* url, const char* postFields,
			BString& response);

// Exchanges an authorization code (or refreshes a token) and stores
// the result through the accounts service. Synchronous: call from a
// worker thread.
bool	OAuth2ExchangeCode(const OAuth2Endpoints& endpoints,
			const char* clientId, const char* code,
			const char* codeVerifier, const char* redirectUri,
			OAuth2Token& token, BString& error);
bool	OAuth2Refresh(const OAuth2Endpoints& endpoints,
			const char* clientId, const char* refreshToken,
			OAuth2Token& token, BString& error);

// Runs the consent flow in an ephemeral BWebView window and posts
// MSG_OAUTH_FINISHED to `target` with the token or an error field.
void	OAuth2RunConsentFlow(const char* providerId,
			const BMessenger& target, const char* accountId,
			const BString& existingRefreshToken);


#endif // WEB_MAIL_OAUTH2_H
