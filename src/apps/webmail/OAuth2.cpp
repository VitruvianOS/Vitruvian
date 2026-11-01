/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WebMailStrings.h"
#include "OAuth2.h"
#include <MailProviders.h>
#include "WebMailMessages.h"
#include <WebDefs.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <File.h>
#include <Message.h>
#include <String.h>
#include <WebView.h>
#include <Window.h>

#include <curl/curl.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>

#include <arpa/inet.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>


namespace {


std::map<std::string, std::string>&
clientIdSettings()
{
	static std::map<std::string, std::string> sIds;
	return sIds;
}


BString
settingsPath()
{
	const char* home = getenv("HOME");
	return BString(home != NULL ? home : "/tmp")
		<< "/config/settings/WebMail/oauth-ids";
}


void
_LoadClientIds()
{
	std::ifstream in(settingsPath().String());
	std::string line;
	while (std::getline(in, line)) {
		size_t split = line.find('=');
		if (split == std::string::npos)
			continue;
		clientIdSettings()[line.substr(0, split)]
			= line.substr(split + 1);
	}
}


void
_SaveClientIds()
{
	std::error_code ec;
	std::filesystem::create_directories(
		std::filesystem::path(settingsPath().String()).parent_path(),
		ec);
	std::ofstream out(settingsPath().String(), std::ios::trunc);
	if (!out.is_open())
		return;
	for (const auto& entry : clientIdSettings())
		out << entry.first << "=" << entry.second << "\n";
}


const char kBase64UrlTable[]
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";


BString
Base64UrlEncode(const uint8* data, size_t length)
{
	BString out;
	size_t i = 0;
	for (; i + 2 < length; i += 3) {
		uint32 triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
		out << kBase64UrlTable[(triple >> 18) & 63]
			<< kBase64UrlTable[(triple >> 12) & 63]
			<< kBase64UrlTable[(triple >> 6) & 63]
			<< kBase64UrlTable[triple & 63];
	}
	if (i + 1 == length) {
		uint32 triple = data[i] << 16;
		out << kBase64UrlTable[(triple >> 18) & 63]
			<< kBase64UrlTable[(triple >> 12) & 63];
	} else if (i + 2 == length) {
		uint32 triple = (data[i] << 16) | (data[i + 1] << 8);
		out << kBase64UrlTable[(triple >> 18) & 63]
			<< kBase64UrlTable[(triple >> 12) & 63]
			<< kBase64UrlTable[(triple >> 6) & 63];
	}
	return out;
}


std::string
JsonStringValue(const char* json, const char* key)
{
	std::string needle = std::string("\"") + key + "\"";
	const char* found = strstr(json, needle.c_str());
	if (found == NULL)
		return std::string();
	found = strchr(found + needle.size(), ':');
	if (found == NULL)
		return std::string();
	found++;
	while (*found == ' ' || *found == '\t')
		found++;
	if (*found != '"') {
		// numbers / bools: read until a delimiter
		std::string value;
		while (*found != '\0' && *found != ',' && *found != '}'
			&& *found != ' ' && *found != '\n') {
			value += *found++;
		}
		return value;
	}
	found++;
	std::string value;
	while (*found != '\0' && *found != '"') {
		if (*found == '\\' && found[1] != '\0') {
			found++;
			switch (*found) {
				case 'n': value += '\n'; break;
				case 't': value += '\t'; break;
				case 'r': value += '\r'; break;
				default: value += *found; break;
			}
		} else {
			value += *found;
		}
		found++;
	}
	return value;
}


size_t
curlWriteCallback(char* ptr, size_t size, size_t count, void* userData)
{
	size_t total = size * count;
	static_cast<BString*>(userData)->Append(ptr, (int32)total);
	return total;
}


} // namespace


//	#pragma mark - public API


bool
OAuth2EndpointsFor(const char* provider, OAuth2Endpoints& endpoints)
{
	if (provider == NULL)
		return false;

	if (strcmp(provider, "google") == 0) {
		endpoints.authorizeUrl
			= "https://accounts.google.com/o/oauth2/v2/auth";
		endpoints.tokenUrl = "https://oauth2.googleapis.com/token";
		endpoints.scope
			= "https://mail.google.com/ "
			  "https://www.googleapis.com/auth/userinfo.email";
		return true;
	}
	if (strcmp(provider, "microsoft") == 0) {
		endpoints.authorizeUrl
			= "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
		endpoints.tokenUrl
			= "https://login.microsoftonline.com/common/oauth2/v2.0/token";
		endpoints.scope
			= "https://outlook.office.com/IMAP.AccessAsUser.All "
			  "https://outlook.office.com/SMTP.Send "
			  "offline_access email";
		return true;
	}
	if (strcmp(provider, "yahoo") == 0) {
		endpoints.authorizeUrl
			= "https://api.login.yahoo.com/v2/request_auth";
		endpoints.tokenUrl = "https://api.login.yahoo.com/v2/get_token";
		endpoints.scope = "mail-r";
		return true;
	}
	return false;
}


BString
OAuth2ClientIdFor(const char* provider)
{
	static bool sLoaded = false;
	if (!sLoaded) {
		_LoadClientIds();
		sLoaded = true;
	}
	auto it = clientIdSettings().find(provider != NULL ? provider : "");
	if (it == clientIdSettings().end())
		return BString();
	return BString(it->second.c_str());
}


void
OAuth2SetClientIdFor(const char* provider, const char* clientId)
{
	static bool sLoaded = false;
	if (!sLoaded) {
		_LoadClientIds();
		sLoaded = true;
	}
	if (provider == NULL)
		return;
	if (clientId == NULL || clientId[0] == '\0')
		clientIdSettings().erase(provider);
	else
		clientIdSettings()[provider] = clientId;
	_SaveClientIds();
}


void
OAuth2MakePKCE(BString& verifier, BString& challenge)
{
	uint8 random[48];
	gnutls_rnd(GNUTLS_RND_RANDOM, random, sizeof(random));
	verifier = Base64UrlEncode(random, sizeof(random));

	uint8 digest[32];
	gnutls_hash_fast(GNUTLS_DIG_SHA256, verifier.String(),
		verifier.Length(), digest);
	challenge = Base64UrlEncode(digest, sizeof(digest));
}


BString
OAuth2MakeState()
{
	uint8 random[24];
	gnutls_rnd(GNUTLS_RND_RANDOM, random, sizeof(random));
	return Base64UrlEncode(random, sizeof(random));
}


BString
OAuth2AuthorizationURL(const OAuth2Endpoints& endpoints,
	const char* clientId, const char* state, const char* codeChallenge)
{
	BString url = endpoints.authorizeUrl;
	url << "?client_id=" << (clientId != NULL ? clientId : "")
		<< "&response_type=code"
		<< "&redirect_uri=" << URLEncode(endpoints.redirectUri.String())
		<< "&scope=" << URLEncode(endpoints.scope.String())
		<< "&state=" << state
		<< "&code_challenge=" << codeChallenge
		<< "&code_challenge_method=S256";
	if (strstr(endpoints.authorizeUrl.String(), "yahoo") != NULL)
		url << "&language=en";
	return url;
}


bool
ParseOAuth2TokenJson(const char* json, OAuth2Token& token, BString& error)
{
	token = OAuth2Token();
	if (json == NULL) {
		error = "empty token response";
		return false;
	}
	std::string accessToken = JsonStringValue(json, "access_token");
	if (accessToken.empty()) {
		error = JsonStringValue(json, "error_description").c_str();
		if (error.Length() == 0) {
			std::string code = JsonStringValue(json, "error");
			error = code.empty()
				? "no access_token in response"
				: code.c_str();
		}
		return false;
	}
	token.accessToken = accessToken.c_str();
	token.refreshToken = JsonStringValue(json, "refresh_token").c_str();
	std::string expiresIn = JsonStringValue(json, "expires_in");
	token.expiresAt = expiresIn.empty()
		? 0 : time(NULL) + atol(expiresIn.c_str());
	return true;
}


bool
OAuth2HttpPost(const char* url, const char* postFields, BString& response)
{
	response = "";
	CURL* curl = curl_easy_init();
	if (curl == NULL)
		return false;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "VOS-WebMail/0.1");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	return result == CURLE_OK;
}


bool
OAuth2ExchangeCode(const OAuth2Endpoints& endpoints, const char* clientId,
	const char* code, const char* codeVerifier, const char* redirectUri,
	OAuth2Token& token, BString& error)
{
	BString fields;
	fields << "grant_type=authorization_code"
		<< "&client_id=" << (clientId != NULL ? clientId : "")
		<< "&code=" << (code != NULL ? code : "")
		<< "&redirect_uri=" << (redirectUri != NULL ? redirectUri : "")
		<< "&code_verifier=" << (codeVerifier != NULL ? codeVerifier : "");

	BString response;
	if (!OAuth2HttpPost(endpoints.tokenUrl.String(), fields.String(),
		response)) {
		error = "token endpoint unreachable";
		return false;
	}
	return ParseOAuth2TokenJson(response.String(), token, error);
}


bool
OAuth2Refresh(const OAuth2Endpoints& endpoints, const char* clientId,
	const char* refreshToken, OAuth2Token& token, BString& error)
{
	if (refreshToken == NULL || refreshToken[0] == '\0') {
		error = "no refresh token";
		return false;
	}
	BString fields;
	fields << "grant_type=refresh_token"
		<< "&client_id=" << (clientId != NULL ? clientId : "")
		<< "&refresh_token=" << refreshToken;

	BString response;
	if (!OAuth2HttpPost(endpoints.tokenUrl.String(), fields.String(),
		response)) {
		error = "token endpoint unreachable";
		return false;
	}
	if (!ParseOAuth2TokenJson(response.String(), token, error))
		return false;
	if (token.refreshToken.Length() == 0)
		token.refreshToken = refreshToken;
	return true;
}


//	#pragma mark - Consent flow


static const char* kConsentDonePage =
	"<!DOCTYPE html><html><head><title>WebMail</title></head>"
	"<body style='font-family:sans-serif;text-align:center;"
	"padding:4em'><h1>Sign-in complete</h1>"
	"<p>You can close this window and return to WebMail.</p>"
	"</body></html>";


static const char* kConsentFailedPage =
	"<!DOCTYPE html><html><head><title>WebMail</title></head>"
	"<body style='font-family:sans-serif;text-align:center;"
	"padding:4em'><h1>Sign-in failed</h1>"
	"<p>Return to WebMail and try again.</p>"
	"</body></html>";


// Loopback state owned by the listener thread; the window only
// flips `quit` when it goes away early.
struct ConsentLoopback {
	int				listenFd;
	int				port;
	volatile bool	quit;
	BMessenger		target;
	BMessenger		window;
	BString			accountId;
	BString			expectedState;
	OAuth2Endpoints	endpoints;
	BString			clientId;
	BString			verifier;
	std::thread		thread;
};


static bool
ParseRequestTarget(const char* request, BString& path, BString& query)
{
	// "GET /?code=...&state=... HTTP/1.1"
	if (strncmp(request, "GET ", 4) != 0)
		return false;
	const char* start = request + 4;
	const char* end = strchr(start, ' ');
	if (end == NULL)
		return false;
	BString target(start, end - start);
	int32 split = target.FindFirst('?');
	if (split == B_ERROR) {
		path = target;
		query = "";
	} else {
		path = StringLeft(target, split);
		query = StringMid(target, split + 1);
	}
	return true;
}


static BString
QueryValue(const BString& query, const char* key)
{
	BString needle(key);
	needle << "=";
	int32 at = query.FindFirst(needle);
	while (at != B_ERROR) {
		if (at == 0 || query[at - 1] == '&')
			break;
		at = query.FindFirst(needle, at + 1);
	}
	if (at == B_ERROR)
		return BString();
	int32 start = at + needle.Length();
	int32 end = query.FindFirst('&', start);
	if (end == B_ERROR)
		end = query.Length();
	return StringMid(query, start, end - start);
}


static void
AnswerClient(int fd, const char* page)
{
	BString response = "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Connection: close\r\n\r\n";
	response << page;
	ssize_t written = write(fd, response.String(), response.Length());
	(void)written;
}


static void
ConsentListenerThread(ConsentLoopback* state)
{
	OAuth2Token token;
	BString error;
	bool authorized = false;
	BString code;

	// Wait for the redirect, giving up after ten minutes; poll so a
	// cancelled consent window stops the listener promptly.
	for (int elapsed = 0; elapsed < 6000 && !state->quit; elapsed++) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(state->listenFd, &readSet);
		struct timeval timeout = {0, 100000};
		int ready = select(state->listenFd + 1, &readSet, NULL, NULL,
			&timeout);
		if (ready <= 0)
			continue;

		int client = accept(state->listenFd, NULL, NULL);
		if (client < 0)
			continue;

		char buffer[8192];
		ssize_t bytes = read(client, buffer, sizeof(buffer) - 1);
		if (bytes > 0) {
			buffer[bytes] = '\0';
			BString path;
			BString query;
			if (ParseRequestTarget(buffer, path, query)) {
				BString errorParam = QueryValue(query, "error");
				BString stateValue = QueryValue(query, "state");
				code = QueryValue(query, "code");
				if (errorParam.Length() > 0) {
					error = "provider said: ";
					error << errorParam;
					AnswerClient(client, kConsentFailedPage);
				} else if (stateValue != state->expectedState) {
					error = "state mismatch in redirect";
					AnswerClient(client, kConsentFailedPage);
				} else if (code.Length() > 0) {
					authorized = true;
					AnswerClient(client, kConsentDonePage);
				} else {
					error = "redirect carried no code";
					AnswerClient(client, kConsentFailedPage);
				}
			} else {
				AnswerClient(client, kConsentDonePage);
			}
		}
		close(client);
		if (authorized || error.Length() > 0)
			break;
	}
	close(state->listenFd);

	if (authorized) {
		if (!OAuth2ExchangeCode(state->endpoints, state->clientId.String(),
			code.String(), state->verifier.String(),
			state->endpoints.redirectUri.String(), token, error)) {
			authorized = false;
		}
	}

	BMessage msg(MSG_OAUTH_FINISHED);
	msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, state->accountId.String());
	if (authorized) {
		msg.AddString(WEBMAIL_CODE_FIELD, token.accessToken.String());
		msg.AddString("webmail:refresh", token.refreshToken.String());
	} else {
		if (error.Length() == 0)
			error = "sign-in cancelled";
		msg.AddString(WEBMAIL_ERROR_FIELD, error.String());
	}
	state->target.SendMessage(&msg);
	state->window.SendMessage(B_QUIT_REQUESTED);

	delete state;
}


class ConsentWindow : public BWindow {
public:
					ConsentWindow(const BString& title, const char* url,
						ConsentLoopback* state)
						:
						BWindow(BRect(120, 120, 820, 820),
							title.String(), B_TITLED_WINDOW,
							B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
						fState(state)
	{
		fView = new BWebView(Bounds(), "consent");
		fView->SetTarget(BMessenger(this));
		fView->SetSession(NULL, true);
		AddChild(fView);
		fView->LoadURL(url);
	}

	virtual				~ConsentWindow()
	{
	}

	virtual void		MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case B_WEB_PAGE_LOAD_FAILED:
				// The loopback answer is the last navigation; a
				// failure here (cancelled by the user) stops the
				// listener through the quit flag.
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

	virtual bool		QuitRequested()
	{
		fState->quit = true;
		return BWindow::QuitRequested();
	}

private:
		BWebView*		fView;
		ConsentLoopback*	fState;
};


void
OAuth2RunConsentFlow(const char* providerId, const BMessenger& target,
	const char* accountId, const BString& existingRefreshToken)
{
	OAuth2Endpoints endpoints;
	if (!OAuth2EndpointsFor(providerId, endpoints)) {
		BMessage msg(MSG_OAUTH_FINISHED);
		msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
		msg.AddString(WEBMAIL_ERROR_FIELD,
			"provider has no OAuth2 endpoints");
		target.SendMessage(&msg);
		return;
	}

	BString clientId = OAuth2ClientIdFor(providerId);
	if (clientId.Length() == 0) {
		BMessage msg(MSG_OAUTH_FINISHED);
		msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
		msg.AddString(WEBMAIL_ERROR_FIELD,
			"no client id configured for this provider");
		target.SendMessage(&msg);
		return;
	}

	// Loopback listener on an ephemeral port.
	int listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd < 0) {
		BMessage msg(MSG_OAUTH_FINISHED);
		msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
		msg.AddString(WEBMAIL_ERROR_FIELD, "cannot open loopback socket");
		target.SendMessage(&msg);
		return;
	}
	int reuse = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (bind(listenFd, (struct sockaddr*)&address, sizeof(address)) != 0
		|| listen(listenFd, 1) != 0) {
		close(listenFd);
		BMessage msg(MSG_OAUTH_FINISHED);
		msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
		msg.AddString(WEBMAIL_ERROR_FIELD, "cannot bind loopback port");
		target.SendMessage(&msg);
		return;
	}
	socklen_t addressLength = sizeof(address);
	getsockname(listenFd, (struct sockaddr*)&address, &addressLength);

	auto* state = new ConsentLoopback();
	state->listenFd = listenFd;
	state->port = ntohs(address.sin_port);
	state->quit = false;
	state->target = target;
	state->accountId = accountId;
	state->clientId = clientId;
	endpoints.redirectUri = BString("http://127.0.0.1:")
		<< state->port << "/";
	state->endpoints = endpoints;
	state->expectedState = OAuth2MakeState();

	BString verifier;
	BString challenge;
	OAuth2MakePKCE(verifier, challenge);
	state->verifier = verifier;

	BString url = OAuth2AuthorizationURL(endpoints, clientId.String(),
		state->expectedState.String(), challenge.String());

	// Refresh-only shortcut: no browser needed when a refresh token
	// is already stored.
	if (existingRefreshToken.Length() > 0) {
		close(listenFd);
		OAuth2Token token;
		BString error;
		if (OAuth2Refresh(endpoints, clientId.String(),
			existingRefreshToken.String(), token, error)) {
			BMessage msg(MSG_OAUTH_FINISHED);
			msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
			msg.AddString(WEBMAIL_CODE_FIELD, token.accessToken.String());
			msg.AddString("webmail:refresh", token.refreshToken.String());
			target.SendMessage(&msg);
		} else {
			BMessage msg(MSG_OAUTH_FINISHED);
			msg.AddString(WEBMAIL_ACCOUNT_ID_FIELD, accountId);
			msg.AddString(WEBMAIL_ERROR_FIELD, error.String());
			target.SendMessage(&msg);
		}
		delete state;
		return;
	}

	BString title = BString("Sign in - ") << providerId;
	ConsentWindow* window = new ConsentWindow(title, url.String(), state);
	state->window = BMessenger(window);
	state->thread = std::thread([state]() {
		ConsentListenerThread(state);
	});
	state->thread.detach();
	window->Show();
}
