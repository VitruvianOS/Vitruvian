/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_ACCOUNTS_H
#define WEB_MAIL_ACCOUNTS_H


#include <String.h>
#include <vector>


enum AccountKind {
	WEB_ACCOUNT,
	IMAP_ACCOUNT,
	POP3_ACCOUNT
};


// One mail account. Web accounts only use the fields that make sense
// for them (id/kind/provider/name/url); native accounts fill the
// protocol fields. Persisted as a flattened BMessage per account.
struct Account {
	BString		id;
	AccountKind	kind;
	BString		provider;		// catalog id, "custom", "manual"
	BString		name;			// display name in the sidebar
	BString		address;		// email address
	BString		url;			// web accounts: entry URL

	// Native protocol servers; ports 0 means "use the default".
	BString		imapHost;
	int32		imapPort;
	bool		imapTLS;		// false: STARTTLS
	BString		pop3Host;
	int32		pop3Port;
	bool		pop3TLS;
	BString		smtpHost;
	int32		smtpPort;
	bool		smtpTLS;

	BString		authMethod;		// "password", "oauth2", "xoauth2"
	BString	 oauthProvider;	// "google", "microsoft", "yahoo"

	BString		identityName;	// From: display name
	BString		signature;
	bool		useIDLE;
	bool		leaveOnServer;
	int32		deleteAfterDays;	// POP3; 0: never

			Account();

	// Stable, filesystem-safe id built from the provider and address.
	BString		MakeId() const;
};


// The one seam between the rest of the app and where accounts and
// credentials live: the system Accounts kit (BAccountRoster plus key
// store secrets) sits behind this interface.
class AccountsService {
public:
	virtual						~AccountsService();

	virtual	void				GetAccounts(std::vector<Account>& accounts) = 0;
	virtual	status_t			LoadAccount(const char* id,
									Account& account) = 0;
	virtual	status_t			SaveAccount(const Account& account) = 0;
	virtual	status_t			DeleteAccount(const char* id) = 0;

	// Named secrets per account ("password", "imap-password",
	// "smtp-password"), stored in the system key store only.
	virtual	status_t			GetSecret(const char* id,
									const char* name, BString& value) = 0;
	virtual	status_t			SetSecret(const char* id,
									const char* name, const char* value) = 0;

	// OAuth2 token blob (access/refresh/expiry, opaque JSON).
	virtual	status_t			GetTokenBlob(const char* id,
									BString& blob) = 0;
	virtual	status_t			SetTokenBlob(const char* id,
									const char* blob) = 0;

	// Directories owned by an account: the web session profile and
	// the offline mail cache.
	virtual	BString				AccountPath(const char* id) const = 0;
	virtual	BString				WebProfilePath(const char* id) const = 0;
	virtual	BString				CachePath(const char* id) const = 0;
	virtual	BString				RootPath() const = 0;
};


// The in-tree implementation. Never NULL.
AccountsService*	DefaultAccountsService();


#endif // WEB_MAIL_ACCOUNTS_H
