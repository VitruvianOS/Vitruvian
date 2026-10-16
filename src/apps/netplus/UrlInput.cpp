/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "UrlInput.h"

#include <ctype.h>
#include <vector>


static const SearchEngine kEngines[] = {
	{ "DuckDuckGo", "https://duckduckgo.com/?q=%s" },
	{ "Wikipedia", "https://en.wikipedia.org/w/index.php?search=%s" },
	{ "Startpage", "https://www.startpage.com/sp/search?query=%s" },
};


static const char* kKnownSchemes[] = {
	"http", "https", "ftp", "file", "mailto", "about", "data",
	"view-source", "vos-about"
};


static std::string
_Trim(const char* text)
{
	if (text == NULL)
		return std::string();

	std::string value(text);
	size_t start = value.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return std::string();
	size_t end = value.find_last_not_of(" \t\r\n");
	return value.substr(start, end - start + 1);
}


static bool
_HasInternalWhitespace(const std::string& text)
{
	for (size_t i = 0; i < text.size(); i++) {
		if (isspace((unsigned char)text[i]))
			return true;
	}
	return false;
}


// scheme ":" rest, letters first, no "//" required. "localhost:8080"
// also matches this shape, so the caller checks the host rules first.
static bool
_SchemePrefix(const std::string& text, std::string* scheme)
{
	size_t colon = text.find(':');
	if (colon == std::string::npos || colon == 0)
		return false;

	for (size_t i = 0; i < colon; i++) {
		char c = (char)tolower((unsigned char)text[i]);
		bool alpha = (c >= 'a' && c <= 'z');
		bool rest = i > 0 && ((c >= '0' && c <= '9') || c == '+'
			|| c == '-' || c == '.');
		if (!alpha && !rest)
			return false;
	}

	if (scheme != NULL)
		*scheme = text.substr(0, colon);
	return true;
}


static bool
_IsKnownScheme(const std::string& scheme)
{
	for (size_t i = 0; i < sizeof(kKnownSchemes) / sizeof(kKnownSchemes[0]);
		i++) {
		if (scheme == kKnownSchemes[i])
			return true;
	}
	return false;
}


static bool
_IsDottedQuad(const std::string& host)
{
	int dots = 0;
	for (size_t i = 0; i < host.size(); i++) {
		char c = host[i];
		if (c == '.') {
			dots++;
			continue;
		}
		if (c < '0' || c > '9')
			return false;
	}
	return dots == 3 && host.size() >= 7;
}


// Host.tld shape: dot-separated labels, alpha TLD of 2+ chars, or an
// all-numeric IPv4 quad. "localhost" counts on its own.
static bool
_HostLooksLikeHost(const std::string& host)
{
	if (host.empty())
		return false;
	if (host == "localhost")
		return true;
	if (_IsDottedQuad(host))
		return true;

	size_t lastDot = host.rfind('.');
	if (lastDot == std::string::npos || lastDot == 0
		|| lastDot + 1 >= host.size()) {
		return false;
	}

	std::string tld = host.substr(lastDot + 1);
	if (tld.size() < 2)
		return false;
	for (size_t i = 0; i < tld.size(); i++) {
		if (!isalpha((unsigned char)tld[i]))
			return false;
	}

	size_t labelStart = 0;
	for (size_t i = 0; i <= host.size(); i++) {
		if (i != host.size() && host[i] != '.')
			continue;
		if (i == labelStart)
			return false;
		char first = host[labelStart];
		char last = host[i - 1];
		if (first == '-' || last == '-')
			return false;
		for (size_t j = labelStart; j < i; j++) {
			char c = host[j];
			bool ok = isalnum((unsigned char)c) || c == '-';
			if (!ok)
				return false;
		}
		labelStart = i + 1;
	}
	return true;
}


static bool
_HostPart(const std::string& text, std::string* host)
{
	size_t end = text.find_first_of("/?#");
	std::string authority = end == std::string::npos
		? text : text.substr(0, end);

	// bracketed IPv6 is not classified as a URL here; strip a numeric
	// port before the shape check so host:8080 still counts.
	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && colon + 1 < authority.size()) {
		bool port = true;
		for (size_t i = colon + 1; i < authority.size(); i++) {
			if (!isdigit((unsigned char)authority[i]))
				port = false;
		}
		if (port)
			authority = authority.substr(0, colon);
	}

	if (host != NULL)
		*host = authority;
	return _HostLooksLikeHost(authority);
}


static std::string
_PercentEncode(const std::string& text)
{
	static const char* kHex = "0123456789ABCDEF";

	std::string encoded;
	encoded.reserve(text.size() * 3);
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = (unsigned char)text[i];
		bool unreserved = isalnum(c) || c == '-' || c == '_' || c == '.'
			|| c == '~';
		if (unreserved) {
			encoded += (char)c;
			continue;
		}
		encoded += '%';
		encoded += kHex[c >> 4];
		encoded += kHex[c & 0x0f];
	}
	return encoded;
}


int32_t
CountSearchEngines()
{
	return (int32_t)(sizeof(kEngines) / sizeof(kEngines[0]));
}


const SearchEngine&
SearchEngineAt(int32_t index)
{
	if (index < 0 || index >= CountSearchEngines())
		index = 0;
	return kEngines[index];
}


UrlInputKind
ClassifyUrlInput(const char* text, std::string* canonicalUrl)
{
	std::string input = _Trim(text);
	if (input.empty())
		return kUrlInputNone;

	// scheme:// is a URL even with odd characters in the rest.
	if (input.find("://") != std::string::npos) {
		if (canonicalUrl != NULL)
			*canonicalUrl = input;
		return kUrlInputUrl;
	}

	std::string scheme;
	if (_SchemePrefix(input, &scheme) && _IsKnownScheme(scheme)) {
		if (canonicalUrl != NULL)
			*canonicalUrl = input;
		return kUrlInputUrl;
	}

	// "localhost:8080" and friends fall through the scheme check and
	// are decided by the host rules; anything with a space is a query.
	if (_HasInternalWhitespace(input))
		return kUrlInputSearch;

	if (_SchemePrefix(input, NULL) && !_HostPart(input, NULL))
		return kUrlInputSearch;

	std::string host;
	if (_HostPart(input, &host)) {
		if (canonicalUrl != NULL)
			*canonicalUrl = "https://" + input;
		return kUrlInputUrl;
	}

	return kUrlInputSearch;
}


std::string
BuildSearchURL(const char* query, int32_t engine)
{
	std::string url = SearchEngineAt(engine).urlTemplate;
	std::string encoded = _PercentEncode(query == NULL ? "" : query);

	size_t marker = url.find("%s");
	if (marker == std::string::npos)
		return url + encoded;

	return url.substr(0, marker) + encoded + url.substr(marker + 2);
}
