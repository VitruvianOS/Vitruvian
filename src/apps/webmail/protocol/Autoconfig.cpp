/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Autoconfig.h"

#include <curl/curl.h>

#include <cstdlib>
#include <resolv.h>
#include <arpa/nameser.h>
#include <cstring>
#include <ctime>


namespace {


size_t
curlWriteCallback(char* ptr, size_t size, size_t count, void* userData)
{
	size_t total = size * count;
	static_cast<BString*>(userData)->Append(ptr, (int32)total);
	return total;
}


// Minimal XML element extraction: the ISPDB documents are flat
// enough that tag matching plus trimming is enough, and it keeps the
// parser free of a DOM dependency.
bool
FindElement(const char* xml, const char* tag, BString& value,
	const char** after = NULL)
{
	BString open(tag);
	open << "<" << tag;
	BString close("</");
	close << tag << ">";
	const char* start = strstr(xml, open.String());
	if (start == NULL)
		return false;
	start += open.Length();
	while (*start == ' ' || *start == '\t' || *start == '\n'
		|| *start == '\r') {
		start++;
	}
	if (*start == '/' && start[1] == '>')	// empty element
		return false;
	const char* end = strstr(start, close.String());
	if (end == NULL)
		return false;
	value.SetTo(start, end - start);
	while (value.Length() > 0
		&& (value[value.Length() - 1] == ' '
			|| value[value.Length() - 1] == '\n'
			|| value[value.Length() - 1] == '\r'
			|| value[value.Length() - 1] == '\t')) {
		value.Truncate(value.Length() - 1);
	}
	if (after != NULL)
		*after = end + close.Length();
	return true;
}


bool
ParseServerBlock(const char* block, AutoconfigServer& server)
{
	BString type;
	if (!FindElement(block, "type", type))
		return false;
	if (!FindElement(block, "hostname", server.host))
		return false;

	BString port;
	if (FindElement(block, "port", port))
		server.port = atoi(port.String());
	BString socketType;
	if (FindElement(block, "socketType", socketType))
		server.socketType = socketType == "SSL" || socketType == "ssl";
	BString auth;
	if (FindElement(block, "authentication", auth)) {
		if (auth == "OAuth2")
			server.auth = "oauth2";
		else
			server.auth = "password";
	}
	BString userName;
	if (FindElement(block, "username", userName))
		server.username = userName;
	return true;
}


} // namespace


AutoconfigResult::AutoconfigResult()
	:
	hasImap(false),
	hasPop3(false),
	hasSmtp(false)
{
}


bool
ParseISPDBXml(const char* xml, AutoconfigResult& result)
{
	if (xml == NULL)
		return false;
	result = AutoconfigResult();

	const char* provider = strstr(xml, "<emailProvider");
	if (provider == NULL)
		return false;

	// Walk the document: every <incomingServer> / <outgoingServer>
	// block tells us its type; keep the first of each.
	const char* scan = provider;
	while ((scan = strstr(scan, "<incomingServer")) != NULL) {
		const char* end = strstr(scan, "</incomingServer>");
		if (end == NULL)
			break;
		BString block(scan, end - scan);
		BString type;
		if (FindElement(block.String(), "type", type)) {
			if (type == "imap" && !result.hasImap) {
				if (ParseServerBlock(block.String(), result.imap))
					result.hasImap = true;
			} else if (type == "pop3" && !result.hasPop3) {
				if (ParseServerBlock(block.String(), result.pop3))
					result.hasPop3 = true;
			}
		}
		scan = end + 1;
	}

	scan = provider;
	while ((scan = strstr(scan, "<outgoingServer")) != NULL) {
		const char* end = strstr(scan, "</outgoingServer>");
		if (end == NULL)
			break;
		BString block(scan, end - scan);
		BString type;
		if (FindElement(block.String(), "type", type) && type == "smtp"
			&& !result.hasSmtp) {
			if (ParseServerBlock(block.String(), result.smtp))
				result.hasSmtp = true;
		}
		scan = end + 1;
	}

	// Optional OAuth2 advertisement (Thunderbird oauth entries).
	const char* oauth = strstr(provider, "<oauth2");
	if (oauth != NULL) {
		const char* end = strstr(oauth, "</oauth2>");
		BString block(oauth,
			end != NULL ? end - oauth : (ptrdiff_t)strlen(oauth));
		FindElement(block.String(), "authUrl", result.oauthAuthUrl);
		FindElement(block.String(), "tokenUrl", result.oauthTokenUrl);
		FindElement(block.String(), "clientId", result.oauthClientId);
		FindElement(block.String(), "scopes", result.oauthScopes);
	}

	return result.hasImap || result.hasPop3 || result.hasSmtp;
}


bool
FetchAutoconfig(const char* address, BString& xml, BString& error)
{
	const char* at = strchr(address, '@');
	if (at == NULL || at == address) {
		error = "address has no domain";
		return false;
	}
	BString domain(at + 1);

	const char* hosts[] = {
		"autoconfig.thunderbird.net",
		"autoconfig.mozilla.org",
		NULL
	};
	for (int32 i = 0; hosts[i] != NULL; i++) {
		BString url = BString("https://") << hosts[i]
			<< "/v1.1/" << address;
		xml = "";
		CURL* curl = curl_easy_init();
		if (curl == NULL)
			continue;
		curl_easy_setopt(curl, CURLOPT_URL, url.String());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &xml);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "VOS-WebMail/0.1");
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
		long status = 0;
		CURLcode result = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);
		if (result == CURLE_OK && status == 200 && xml.Length() > 0)
			return true;
	}
	error = BString("no ISPDB entry for ") << domain;
	return false;
}


bool
ResolveAutoconfig(const char* address, AutoconfigResult& result,
	BString& error)
{
	BString xml;
	if (!FetchAutoconfig(address, xml, error))
		return false;
	return ParseISPDBXml(xml.String(), result);
}


bool
LookupSRV(const char* service, const char* domain, BString& host,
	int32& port)
{
	if (service == NULL || domain == NULL)
		return false;

	BString query = BString(service) << "." << domain;
	unsigned char answer[2048];
	int length = res_query(query.String(), C_IN, T_SRV, answer,
		sizeof(answer));
	if (length < 0)
		return false;

	ns_msg message;
	if (ns_initparse(answer, length, &message) < 0)
		return false;

	int count = ns_msg_count(message, ns_s_an);
	for (int i = 0; i < count; i++) {
		ns_rr record;
		if (ns_parserr(&message, ns_s_an, i, &record) < 0)
			continue;
		if (ns_rr_type(record) != T_SRV)
			continue;
		if (ns_rr_rdlen(record) < 6)
			continue;
		const unsigned char* data = (const unsigned char*)ns_rr_rdata(record);
		uint16_t priority = (data[0] << 8) | data[1];
		(void)priority;
		port = (data[2] << 8) | data[3];
		int targetLength = data[4];
		char target[256];
		if (dn_expand(ns_msg_base(message), ns_msg_base(message)
			+ (ns_msg_end(message) - ns_msg_base(message)), data + 5, target,
			sizeof(target)) < 0) {
			continue;
		}
		(void)targetLength;
		host = target;
		while (host.Length() > 0 && host[host.Length() - 1] == '.')
			host.Truncate(host.Length() - 1);
		return true;
	}
	return false;
}
