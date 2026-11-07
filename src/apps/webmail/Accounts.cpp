/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Accounts.h"

#include <Account.h>
#include <AccountRoster.h>
#include <Key.h>
#include <KeyStore.h>

#include <cstring>
#include <filesystem>


namespace {


const char* kKindWeb = "web";
const char* kKindImap = "imap";
const char* kKindPop3 = "pop3";

const char* kTokenSecret = "tokens";


const char*
KindToString(AccountKind kind)
{
	switch (kind) {
		case WEB_ACCOUNT:
			return kKindWeb;
		case IMAP_ACCOUNT:
			return kKindImap;
		case POP3_ACCOUNT:
			return kKindPop3;
	}
	return kKindWeb;
}


AccountKind
KindFromString(const char* kind)
{
	if (kind != NULL && strcmp(kind, kKindImap) == 0)
		return IMAP_ACCOUNT;
	if (kind != NULL && strcmp(kind, kKindPop3) == 0)
		return POP3_ACCOUNT;
	return WEB_ACCOUNT;
}


} // namespace


Account::Account()
	:
	kind(WEB_ACCOUNT),
	imapPort(0),
	imapTLS(true),
	pop3Port(0),
	pop3TLS(true),
	smtpPort(0),
	smtpTLS(true),
	useIDLE(true),
	leaveOnServer(true),
	deleteAfterDays(0)
{
}


BString
Account::MakeId() const
{
	BString base = provider;
	if (base.Length() == 0)
		base = "account";
	base << "-" << address;
	if (base.Length() > 80)
		base.Truncate(80);

	BString id;
	for (int32 i = 0; i < base.Length(); i++) {
		char c = base[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '.') {
			id << c;
		} else {
			id << '_';
		}
	}
	return id;
}


AccountsService::~AccountsService()
{
}


//	#pragma mark - SystemAccountsService


// The account store is the system Accounts service: the roster keeps
// one flattened message per account and every secret goes through the
// key store. A key store refusal reaches the caller; there is no
// plaintext fallback.
class SystemAccountsService : public AccountsService {
public:
	virtual void				GetAccounts(std::vector<Account>& accounts);
	virtual status_t			LoadAccount(const char* id,
									Account& account);
	virtual status_t			SaveAccount(const Account& account);
	virtual status_t			DeleteAccount(const char* id);

	virtual status_t			GetSecret(const char* id,
									const char* name, BString& value);
	virtual status_t			SetSecret(const char* id,
									const char* name, const char* value);

	virtual status_t			GetTokenBlob(const char* id,
									BString& blob);
	virtual status_t			SetTokenBlob(const char* id,
									const char* blob);

	virtual BString				AccountPath(const char* id) const;
	virtual BString				WebProfilePath(const char* id) const;
	virtual BString				CachePath(const char* id) const;
	virtual BString				RootPath() const;

private:
			void				_FromRoster(const BAccount& source,
									Account& account) const;
			status_t			_ToRoster(const Account& account,
									BAccount& target) const;
			BString				_SecretId(const char* id,
									const char* name) const;
			BAccountRoster*		_Roster() const;
};


BAccountRoster*
SystemAccountsService::_Roster() const
{
	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL || roster->InitCheck() != B_OK)
		return NULL;
	return roster;
}


BString
SystemAccountsService::_SecretId(const char* id, const char* name) const
{
	return BString("webmail:") << id << ":" << name;
}


BString
SystemAccountsService::RootPath() const
{
	const char* home = getenv("HOME");
	return BString(home != NULL ? home : "/tmp")
		<< "/config/settings/WebMail";
}


BString
SystemAccountsService::AccountPath(const char* id) const
{
	return BString(RootPath()) << "/accounts/" << id;
}


BString
SystemAccountsService::WebProfilePath(const char* id) const
{
	return BString(AccountPath(id)) << "/web";
}


BString
SystemAccountsService::CachePath(const char* id) const
{
	return BString(AccountPath(id)) << "/cache";
}


void
SystemAccountsService::_FromRoster(const BAccount& source,
	Account& account) const
{
	account = Account();
	account.id = source.Id();
	account.provider = source.ProviderId();
	account.name = source.Label();
	account.address = source.UserName();

	BString text;
	int32 integer = 0;
	bool flag = false;

	if (source.GetSetting("kind", text) == B_OK)
		account.kind = KindFromString(text.String());

	if (source.GetSetting("url", text) == B_OK)
		account.url = text;
	if (source.GetSetting("imap_host", text) == B_OK)
		account.imapHost = text;
	if (source.GetSetting("imap_port", integer) == B_OK)
		account.imapPort = integer;
	if (source.GetSetting("imap_tls", flag) == B_OK)
		account.imapTLS = flag;
	if (source.GetSetting("pop3_host", text) == B_OK)
		account.pop3Host = text;
	if (source.GetSetting("pop3_port", integer) == B_OK)
		account.pop3Port = integer;
	if (source.GetSetting("pop3_tls", flag) == B_OK)
		account.pop3TLS = flag;
	if (source.GetSetting("smtp_host", text) == B_OK)
		account.smtpHost = text;
	if (source.GetSetting("smtp_port", integer) == B_OK)
		account.smtpPort = integer;
	if (source.GetSetting("smtp_tls", flag) == B_OK)
		account.smtpTLS = flag;

	if (source.GetSetting("auth", text) == B_OK)
		account.authMethod = text;
	if (source.GetSetting("oauth_provider", text) == B_OK)
		account.oauthProvider = text;
	if (source.GetSetting("identity_name", text) == B_OK)
		account.identityName = text;
	if (source.GetSetting("signature", text) == B_OK)
		account.signature = text;
	if (source.GetSetting("use_idle", flag) == B_OK)
		account.useIDLE = flag;
	if (source.GetSetting("leave_on_server", flag) == B_OK)
		account.leaveOnServer = flag;
	if (source.GetSetting("delete_after_days", integer) == B_OK)
		account.deleteAfterDays = integer;
}


status_t
SystemAccountsService::_ToRoster(const Account& account,
	BAccount& target) const
{
	if (account.id.Length() == 0 || account.provider.Length() == 0)
		return B_BAD_VALUE;

	target.SetId(account.id.String());
	target.SetProviderId(account.provider.String());
	target.SetLabel(account.name.String());
	target.SetUserName(account.address.String());

	target.SetSetting("webmail", true);
	target.SetSetting("kind", KindToString(account.kind));

	status_t status = target.SetSetting("url", account.url.String());
	if (status == B_OK)
		status = target.SetSetting("imap_host", account.imapHost.String());
	if (status == B_OK)
		status = target.SetSetting("imap_port", account.imapPort);
	if (status == B_OK)
		status = target.SetSetting("imap_tls", account.imapTLS);
	if (status == B_OK)
		status = target.SetSetting("pop3_host", account.pop3Host.String());
	if (status == B_OK)
		status = target.SetSetting("pop3_port", account.pop3Port);
	if (status == B_OK)
		status = target.SetSetting("pop3_tls", account.pop3TLS);
	if (status == B_OK)
		status = target.SetSetting("smtp_host", account.smtpHost.String());
	if (status == B_OK)
		status = target.SetSetting("smtp_port", account.smtpPort);
	if (status == B_OK)
		status = target.SetSetting("smtp_tls", account.smtpTLS);
	if (status == B_OK)
		status = target.SetSetting("auth", account.authMethod.String());
	if (status == B_OK)
		status = target.SetSetting("oauth_provider",
			account.oauthProvider.String());
	if (status == B_OK)
		status = target.SetSetting("identity_name",
			account.identityName.String());
	if (status == B_OK)
		status = target.SetSetting("signature", account.signature.String());
	if (status == B_OK)
		status = target.SetSetting("use_idle", account.useIDLE);
	if (status == B_OK)
		status = target.SetSetting("leave_on_server", account.leaveOnServer);
	if (status == B_OK)
		status = target.SetSetting("delete_after_days",
			account.deleteAfterDays);
	return status;
}


void
SystemAccountsService::GetAccounts(std::vector<Account>& accounts)
{
	BAccountRoster* roster = _Roster();
	if (roster == NULL)
		return;

	uint32 cookie = 0;
	BAccount source;
	while (roster->GetNextAccount(cookie, source) == B_OK) {
		bool mine = false;
		if (source.GetSetting("webmail", mine) != B_OK || !mine)
			continue;
		Account account;
		_FromRoster(source, account);
		accounts.push_back(account);
	}
}


status_t
SystemAccountsService::LoadAccount(const char* id, Account& account)
{
	if (id == NULL || id[0] == '\0')
		return B_BAD_VALUE;

	BAccountRoster* roster = _Roster();
	if (roster == NULL)
		return B_ERROR;

	BAccount source;
	status_t status = roster->FindAccount(id, source);
	if (status != B_OK)
		return status;

	bool mine = false;
	if (source.GetSetting("webmail", mine) != B_OK || !mine)
		return B_ENTRY_NOT_FOUND;

	_FromRoster(source, account);
	return B_OK;
}


status_t
SystemAccountsService::SaveAccount(const Account& account)
{
	if (account.id.Length() == 0)
		return B_BAD_VALUE;

	BAccountRoster* roster = _Roster();
	if (roster == NULL)
		return B_ERROR;

	BAccount target;
	status_t status = _ToRoster(account, target);
	if (status != B_OK)
		return status;

	BAccount existing;
	if (roster->FindAccount(account.id.String(), existing) == B_OK)
		return roster->UpdateAccount(target);
	return roster->AddAccount(target);
}


status_t
SystemAccountsService::DeleteAccount(const char* id)
{
	if (id == NULL)
		return B_BAD_VALUE;

	BAccountRoster* roster = _Roster();
	if (roster == NULL)
		return B_ERROR;

	// Best effort: the account goes away even if the key store
	// refuses to drop its secrets.
	BKeyStore store;
	const char* names[] = { "password", "imap-password", "smtp-password",
		kTokenSecret };
	for (int32 i = 0; i < 4; i++) {
		BKey key(B_KEY_PURPOSE_NETWORK, _SecretId(id, names[i]).String(),
			"password");
		store.RemoveKey(key);
	}
	return roster->RemoveAccount(id);
}


status_t
SystemAccountsService::GetSecret(const char* id, const char* name,
	BString& value)
{
	if (id == NULL || name == NULL)
		return B_BAD_VALUE;

	BKeyStore store;
	BKey key;
	status_t status = store.GetKey(B_KEY_TYPE_PASSWORD,
		_SecretId(id, name).String(), key);
	if (status != B_OK)
		return status;
	if (key.DataLength() == 0)
		return B_ENTRY_NOT_FOUND;

	value.SetTo((const char*)key.Data(), key.DataLength());
	return B_OK;
}


status_t
SystemAccountsService::SetSecret(const char* id, const char* name,
	const char* value)
{
	if (id == NULL || name == NULL || value == NULL)
		return B_BAD_VALUE;

	BKey key(B_KEY_PURPOSE_NETWORK, _SecretId(id, name).String(),
		"password", (const uint8*)value, strlen(value));
	BKeyStore store;
	store.RemoveKey(key);
	return store.AddKey(key);
}


status_t
SystemAccountsService::GetTokenBlob(const char* id, BString& blob)
{
	return GetSecret(id, kTokenSecret, blob);
}


status_t
SystemAccountsService::SetTokenBlob(const char* id, const char* blob)
{
	return SetSecret(id, kTokenSecret, blob);
}


/* static */
AccountsService*
DefaultAccountsService()
{
	static SystemAccountsService sService;
	return &sService;
}
