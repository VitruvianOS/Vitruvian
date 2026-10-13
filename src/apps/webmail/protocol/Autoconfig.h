/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_AUTOCONFIG_H
#define WEB_MAIL_AUTOCONFIG_H


#include <String.h>
#include <vector>


struct AutoconfigServer {
	BString		host;
	int32		port;
	bool		socketType;		// true: TLS, false: STARTTLS/plaintext
	BString		auth;			// "password", "oauth2", ...
	BString		username;		// "%EMAILADDRESS%" etc.
};


struct AutoconfigResult {
	AutoconfigServer	imap;
	AutoconfigServer	pop3;
	AutoconfigServer	smtp;
	bool				hasImap;
	bool				hasPop3;
	bool				hasSmtp;
	// OAuth2 endpoints advertised by the provider, when present.
	BString				oauthAuthUrl;
	BString				oauthTokenUrl;
	BString				oauthClientId;
	BString				oauthScopes;

						AutoconfigResult();
};


// Pure XML parsing of a Mozilla ISPDB / autoconfig document (host
// tested). Handles <emailProvider> with imap/pop3/smtp
// <incomingServer>/<outgoingServer> and optional oauth2 entries.
bool	ParseISPDBXml(const char* xml, AutoconfigResult& result);

// Fetches the ISPDB document for an address over HTTPS
// (autoconfig.thunderbird.net first, then autoconfig.mozilla.org).
// Returns false when neither host carries the domain.
bool	FetchAutoconfig(const char* address, BString& xml, BString& error);

// Full lookup: fetch + parse. Call from a worker thread.
bool	ResolveAutoconfig(const char* address, AutoconfigResult& result,
			BString& error);

// DNS SRV lookup for "_imaps._tcp.<domain>" / "_submission._tcp";
// fills the host when a record exists. Returns false on NXDOMAIN.
bool	LookupSRV(const char* service, const char* domain,
			BString& host, int32& port);


#endif // WEB_MAIL_AUTOCONFIG_H
