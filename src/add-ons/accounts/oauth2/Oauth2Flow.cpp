/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Oauth2Flow.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>

#include <Key.h>
#include <KeyStore.h>
#include <openssl/sha.h>

#include <AccountsDefs.h>

#include "HttpsClient.h"


static const char* kKeyringName = "Accounts";
static const char* kAccessTokenId = "access_token";
static const char* kRefreshTokenId = "refresh_token";
static const char* kExpiryId = "expiry";

static const bigtime_t kExpiryMargin = 60000000LL;
static const int kConsentTimeoutSeconds = 300;


static void
_RandomString(BString& out, int32 length)
{
	static const char* alphabet
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

	uint8 bytes[128];
	if (length > (int32)sizeof(bytes))
		length = sizeof(bytes);

	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got = -1;
	if (fd >= 0) {
		got = read(fd, bytes, length);
		close(fd);
	}
	if (got != length)
		memset(bytes, 0, length);

	out = "";
	for (int32 i = 0; i < length; i++)
		out << alphabet[bytes[i] % (strlen(alphabet))];
}


static void
_Base64Url(const uint8* data, size_t length, BString& out)
{
	static const char* table
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	out = "";
	for (size_t i = 0; i < length; i += 3) {
		uint32 value = (uint32)data[i] << 16;
		if (i + 1 < length)
			value |= (uint32)data[i + 1] << 8;
		if (i + 2 < length)
			value |= data[i + 2];

		out << table[(value >> 18) & 0x3f];
		out << table[(value >> 12) & 0x3f];
		if (i + 1 < length)
			out << table[(value >> 6) & 0x3f];
		if (i + 2 < length)
			out << table[value & 0x3f];
	}
}


static void
_PkceChallenge(const BString& verifier, BString& challenge)
{
	uint8 hash[SHA256_DIGEST_LENGTH];
	SHA256((const uint8*)verifier.String(), verifier.Length(), hash);
	_Base64Url(hash, sizeof(hash), challenge);
}


static status_t
_ParseFormValue(const BString& response, const char* name, BString& value)
{
	BString needle(name);
	needle << "=";

	const char* start = strstr(response.String(), needle.String());
	if (start == NULL)
		return B_NAME_NOT_FOUND;

	start += needle.Length();
	const char* end = start;
	while (*end != '\0' && *end != '&' && *end != '\r' && *end != '\n')
		end++;

	value.SetTo(start, end - start);
	return B_OK;
}


static status_t
_TokenRequest(const char* tokenUri, const BString& body, BString& accessToken,
	BString& refreshToken, bigtime_t& expiry)
{
	BString response;
	status_t status = HttpsPost(tokenUri, body, response);
	if (status != B_OK)
		return status;

	if (_ParseFormValue(response, "access_token", accessToken) != B_OK)
		return B_ERROR;

	refreshToken = "";
	_ParseFormValue(response, "refresh_token", refreshToken);

	int32 expiresIn = 3600;
	BString expiresText;
	if (_ParseFormValue(response, "expires_in", expiresText) == B_OK)
		expiresIn = atoi(expiresText.String());

	expiry = (bigtime_t)(time(NULL) + expiresIn) * 1000000LL;
	return B_OK;
}


status_t
Oauth2Flow::Store(const BAccount& account, const BString& accessToken,
	const BString& refreshToken, bigtime_t expiry)
{
	BKeyStore store;

	BKey accessKey(B_KEY_PURPOSE_NETWORK, account.Id(), kAccessTokenId,
		(const uint8*)accessToken.String(), accessToken.Length());
	status_t status = store.AddKey(kKeyringName, accessKey);
	if (status != B_OK) {
		status = store.AddKeyring(kKeyringName);
		if (status != B_OK && status != B_FILE_EXISTS)
			return status;

		status = store.AddKey(kKeyringName, accessKey);
		if (status != B_OK)
			return status;
	}

	if (refreshToken.Length() > 0) {
		BKey refreshKey(B_KEY_PURPOSE_NETWORK, account.Id(),
			kRefreshTokenId, (const uint8*)refreshToken.String(),
			refreshToken.Length());
		store.AddKey(kKeyringName, refreshKey);
	}

	BKey expiryKey(B_KEY_PURPOSE_NETWORK, account.Id(), kExpiryId,
		(const uint8*)&expiry, sizeof(expiry));
	store.AddKey(kKeyringName, expiryKey);
	return B_OK;
}


status_t
Oauth2Flow::Load(const BAccount& account, BString& accessToken,
	BString& refreshToken, bigtime_t& expiry)
{
	BKeyStore store;

	BKey accessKey;
	status_t status = store.GetKey(kKeyringName, B_KEY_TYPE_GENERIC,
		account.Id(), kAccessTokenId, accessKey);
	if (status != B_OK)
		return status;

	if (accessKey.Data() == NULL)
		return B_ERROR;

	accessToken.SetTo((const char*)accessKey.Data(), accessKey.DataLength());

	refreshToken = "";
	BKey refreshKey;
	if (store.GetKey(kKeyringName, B_KEY_TYPE_GENERIC, account.Id(),
			kRefreshTokenId, refreshKey) == B_OK
		&& refreshKey.Data() != NULL) {
		refreshToken.SetTo((const char*)refreshKey.Data(),
			refreshKey.DataLength());
	}

	expiry = 0;
	BKey expiryKey;
	if (store.GetKey(kKeyringName, B_KEY_TYPE_GENERIC, account.Id(),
			kExpiryId, expiryKey) == B_OK
		&& expiryKey.DataLength() == sizeof(bigtime_t)) {
		memcpy(&expiry, expiryKey.Data(), sizeof(expiry));
	}

	return B_OK;
}


status_t
Oauth2Flow::Remove(const BAccount& account)
{
	BKeyStore store;

	BKey accessKey(B_KEY_PURPOSE_NETWORK, account.Id(), kAccessTokenId);
	BKey refreshKey(B_KEY_PURPOSE_NETWORK, account.Id(), kRefreshTokenId);
	BKey expiryKey(B_KEY_PURPOSE_NETWORK, account.Id(), kExpiryId);

	store.RemoveKey(kKeyringName, accessKey);
	store.RemoveKey(kKeyringName, refreshKey);
	store.RemoveKey(kKeyringName, expiryKey);
	return B_OK;
}


static int32
_ListenLoopback(int& port)
{
	int socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (socket < 0)
		return -1;

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;

	if (bind(socket, (struct sockaddr*)&address, sizeof(address)) != 0
		|| listen(socket, 1) != 0) {
		close(socket);
		return -1;
	}

	socklen_t length = sizeof(address);
	if (getsockname(socket, (struct sockaddr*)&address, &length) != 0) {
		close(socket);
		return -1;
	}

	port = ntohs(address.sin_port);
	return socket;
}


static void
_SendPage(int client, int status, const char* body)
{
	char header[256];
	snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n",
		status, status == 200 ? "OK" : "Bad Request", (int)strlen(body));
	if (write(client, header, strlen(header)) < 0)
		return;
	ssize_t written = write(client, body, strlen(body));
	(void)written;
}


static status_t
_WaitForCode(int socket, const BString& state, BString& code)
{
	const char* donePage
		= "<html><body>Authorized. You may close this.</body></html>";
	const char* badPage
		= "<html><body>Denied. You may close this.</body></html>";

	bigtime_t deadline = system_time()
		+ (bigtime_t)kConsentTimeoutSeconds * 1000000;

	// Browsers probe the listener before the redirect lands, so keep
	// accepting until one request carries our state or the consent
	// window times out.
	while (true) {
		fd_set set;
		FD_ZERO(&set);
		FD_SET(socket, &set);

		bigtime_t remaining = deadline - system_time();
		if (remaining <= 0)
			return B_TIMED_OUT;

		struct timeval timeout;
		timeout.tv_sec = remaining / 1000000;
		timeout.tv_usec = remaining % 1000000;

		if (select(socket + 1, &set, NULL, NULL, &timeout) <= 0)
			return B_TIMED_OUT;

		int client = accept(socket, NULL, NULL);
		if (client < 0)
			return B_ERROR;

		char buffer[2048];
		ssize_t bytes = read(client, buffer, sizeof(buffer) - 1);
		if (bytes <= 0) {
			close(client);
			continue;
		}
		buffer[bytes] = '\0';

		// Only the request line carries the redirect's query.
		char* end = strpbrk(buffer, "\r\n");
		if (end != NULL)
			*end = '\0';
		BString requestLine(buffer);

		const char* stateStart = strstr(requestLine.String(), "state=");
		if (stateStart == NULL) {
			_SendPage(client, 400, badPage);
			close(client);
			continue;
		}

		stateStart += strlen("state=");
		const char* stateEnd = stateStart;
		while (*stateEnd != '\0' && *stateEnd != '&' && *stateEnd != ' ')
			stateEnd++;

		BString receivedState;
		receivedState.SetTo(stateStart, stateEnd - stateStart);
		if (receivedState != state) {
			_SendPage(client, 400, badPage);
			close(client);
			continue;
		}

		const char* codeStart = strstr(requestLine.String(), "code=");
		if (codeStart != NULL) {
			codeStart += strlen("code=");
			const char* codeEnd = codeStart;
			while (*codeEnd != '\0' && *codeEnd != '&' && *codeEnd != ' ')
				codeEnd++;
			code.SetTo(codeStart, codeEnd - codeStart);
		}

		_SendPage(client, 200, donePage);
		close(client);
		return code.Length() > 0 ? B_OK : B_ERROR;
	}
}


status_t
Oauth2Flow::RunInteractive(BAccount& account, const BMessenger& consentTo,
	BMessage& token)
{
	BString clientId;
	BString authUri;
	BString tokenUri;
	BString scope;
	if (account.GetSetting("client_id", clientId) != B_OK
		|| account.GetSetting("auth_uri", authUri) != B_OK
		|| account.GetSetting("token_uri", tokenUri) != B_OK) {
		return B_BAD_VALUE;
	}
	account.GetSetting("scope", scope);

	int port = 0;
	int listener = _ListenLoopback(port);
	if (listener < 0)
		return B_ERROR;

	BString verifier;
	BString challenge;
	_RandomString(verifier, 64);
	_PkceChallenge(verifier, challenge);

	BString state;
	_RandomString(state, 16);

	BString redirect;
	redirect.SetToFormat("http://127.0.0.1:%d/", port);

	BString encodedScope;
	UrlEncode(scope, encodedScope);

	BString url = authUri;
	url << (strstr(authUri.String(), "?") != NULL ? "&" : "?");
	url << "client_id=" << clientId;
	url << "&response_type=code";
	url << "&redirect_uri=" << redirect;
	url << "&state=" << state;
	url << "&code_challenge=" << challenge;
	url << "&code_challenge_method=S256";
	if (scope.Length() > 0)
		url << "&scope=" << encodedScope;

	BMessage consent(kAccountConsentRequest);
	consent.AddString(kAccountsUrlField, url);
	consentTo.SendMessage(&consent);

	BString code;
	status_t status = _WaitForCode(listener, state, code);
	close(listener);
	if (status != B_OK)
		return status;

	BString body("grant_type=authorization_code");
	body << "&client_id=" << clientId;
	body << "&code=" << code;
	BString encodedRedirect;
	UrlEncode(redirect, encodedRedirect);
	body << "&redirect_uri=" << encodedRedirect;
	body << "&code_verifier=" << verifier;

	BString accessToken;
	BString refreshToken;
	bigtime_t expiry = 0;
	status = _TokenRequest(tokenUri.String(), body, accessToken, refreshToken,
		expiry);
	if (status != B_OK)
		return status;

	status = Store(account, accessToken, refreshToken, expiry);
	if (status != B_OK)
		return status;

	token.MakeEmpty();
	token.AddString(kAccountsTokenField, accessToken);
	token.AddInt64(kAccountsExpiryField, expiry);
	return B_OK;
}


status_t
Oauth2Flow::Refresh(BAccount& account, BMessage& token)
{
	BString clientId;
	BString tokenUri;
	if (account.GetSetting("client_id", clientId) != B_OK
		|| account.GetSetting("token_uri", tokenUri) != B_OK) {
		return B_BAD_VALUE;
	}

	BString accessToken;
	BString refreshToken;
	bigtime_t expiry = 0;
	status_t status = Load(account, accessToken, refreshToken, expiry);
	if (status != B_OK)
		return status;

	if (refreshToken.Length() == 0)
		return B_NO_INIT;

	BString body("grant_type=refresh_token");
	body << "&client_id=" << clientId;
	body << "&refresh_token=" << refreshToken;

	status = _TokenRequest(tokenUri.String(), body, accessToken, refreshToken,
		expiry);
	if (status != B_OK)
		return status;

	status = Store(account, accessToken, refreshToken, expiry);
	if (status != B_OK)
		return status;

	token.MakeEmpty();
	token.AddString(kAccountsTokenField, accessToken);
	token.AddInt64(kAccountsExpiryField, expiry);
	return B_OK;
}
