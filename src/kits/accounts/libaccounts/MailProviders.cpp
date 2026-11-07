/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <MailProviders.h>

#include <cstring>
#include <vector>


static const WebProvider kProviders[] = {
	{ "gmail", "Gmail",
		"https://mail.google.com/mail/u/0/",
		"https://mail.google.com/mail/u/0/?view=cm&fs=1&authuser=0&to=%s",
		"google", NULL },
	{ "outlook", "Outlook.com",
		"https://outlook.live.com/mail/",
		"https://outlook.live.com/mail/0/deeplink/compose?to=%s",
		"microsoft", NULL },
	{ "microsoft365", "Microsoft 365",
		"https://outlook.office.com/mail/",
		"https://outlook.office.com/mail/0/deeplink/compose?to=%s",
		"microsoft", NULL },
	{ "yahoo", "Yahoo Mail",
		"https://mail.yahoo.com/",
		"https://compose.mail.yahoo.com/?to=%s",
		"yahoo", NULL },
	{ "icloud", "iCloud Mail",
		"https://www.icloud.com/mail",
		"https://www.icloud.com/mail/#compose?to=%s",
		NULL, NULL },
	{ "proton", "Proton Mail",
		"https://mail.proton.me/u/0/inbox",
		"https://mail.proton.me/u/0/all-mail?to=%s",
		NULL, NULL },
	{ "fastmail", "Fastmail",
		"https://app.fastmail.com/mail/Inbox",
		"https://app.fastmail.com/mail/compose?to=%s",
		NULL, NULL },
	{ "zoho", "Zoho Mail",
		"https://mail.zoho.com/",
		"https://mail.zoho.com/zm/#compose/to=%s",
		NULL, NULL },
	{ "gmx", "GMX",
		"https://www.gmx.net/mail/",
		"https://compose.mail.yahoo.com/?to=%s",
		NULL, NULL },
	{ "webde", "WEB.DE",
		"https://www.web.de/mail/",
		"https://compose.mail.yahoo.com/?to=%s",
		NULL, NULL },
	{ "mailcom", "Mail.com",
		"https://www.mail.com/mail/",
		"https://compose.mail.yahoo.com/?to=%s",
		NULL, NULL },
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};


const WebProvider*
FindWebProvider(const char* id)
{
	if (id == NULL)
		return NULL;
	for (int32 i = 0; kProviders[i].id != NULL; i++) {
		if (strcmp(kProviders[i].id, id) == 0)
			return &kProviders[i];
	}
	return NULL;
}


void
GetWebProviders(std::vector<WebProvider>& providers)
{
	for (int32 i = 0; kProviders[i].id != NULL; i++)
		providers.push_back(kProviders[i]);
}


BString
URLEncode(const char* text)
{
	BString encoded;
	if (text == NULL)
		return encoded;

	static const char* kHex = "0123456789ABCDEF";
	for (const unsigned char* c = (const unsigned char*)text;
		*c != '\0'; c++) {
		if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
			|| (*c >= '0' && *c <= '9') || *c == '-' || *c == '_'
			|| *c == '.' || *c == '~') {
			encoded << (char)*c;
		} else {
			encoded << '%' << kHex[*c >> 4] << kHex[*c & 0x0F];
		}
	}
	return encoded;
}


bool
ComposeURLFor(const WebProvider* provider, const char* address, BString& url)
{
	if (provider == NULL || provider->composeUrl == NULL
		|| provider->composeUrl[0] == '\0') {
		return false;
	}

	BString encoded = URLEncode(address);
	const char* templateUrl = provider->composeUrl;
	const char* placeholder = strstr(templateUrl, "%s");
	if (placeholder == NULL) {
		url = templateUrl;
		return true;
	}

	url.Append(templateUrl, placeholder - templateUrl);
	url << encoded;
	url << (placeholder + 2);
	return true;
}
