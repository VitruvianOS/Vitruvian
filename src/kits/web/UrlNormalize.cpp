/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */

#include <web/UrlNormalize.h>

#include <cctype>


const char* const kWebSearchDuckDuckGo
	= "https://duckduckgo.com/?q=%s";
const char* const kWebSearchStartpage
	= "https://www.startpage.com/sp/search?query=%s";
const char* const kWebSearchBrave
	= "https://search.brave.com/search?q=%s";
const char* const kWebSearchGoogle
	= "https://www.google.com/search?q=%s";
const char* const kWebSearchBing
	= "https://www.bing.com/search?q=%s";
const char* const kWebSearchWikipedia
	= "https://en.wikipedia.org/wiki/Special:Search?search=%s";


std::string
web_url_encode(const std::string& value)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string result;
	result.reserve(value.size() * 3);
	for (unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.'
			|| c == '~') {
			result += (char)c;
		} else {
			result += '%';
			result += kHex[c >> 4];
			result += kHex[c & 0xf];
		}
	}
	return result;
}


static std::string
trim(const std::string& value)
{
	size_t start = 0;
	size_t end = value.size();
	while (start < end && std::isspace((unsigned char)value[start]))
		start++;
	while (end > start && std::isspace((unsigned char)value[end - 1]))
		end--;
	return value.substr(start, end - start);
}


static bool
hasScheme(const std::string& input)
{
	size_t colon = input.find(':');
	if (colon == std::string::npos || colon == 0)
		return false;
	// "host:8080" has a digit right after the colon; no scheme looks
	// like that, and the classic browsers all use this heuristic.
	if (colon + 1 < input.size()
		&& std::isdigit((unsigned char)input[colon + 1]))
		return false;
	// A scheme is ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ).
	for (size_t i = 0; i < colon; i++) {
		unsigned char c = (unsigned char)input[i];
		if (i == 0) {
			if (!std::isalpha(c))
				return false;
		} else if (!std::isalnum(c) && c != '+' && c != '-' && c != '.')
			return false;
	}
	return true;
}


static bool
looksLikeHost(const std::string& input)
{
	size_t colon = input.find(':');
	std::string host = colon != std::string::npos
		? input.substr(0, colon) : input;
	if (host.empty())
		return false;
	if (host == "localhost")
		return true;
	// "example.com", "sub.example.co.uk:8080", "192.168.0.1".
	// A bare word with no dot is a search, not a host.
	return host.find('.') != std::string::npos;
}


std::string
web_url_for_user_input(const std::string& input,
	const std::string& searchTemplate)
{
	std::string text = trim(input);
	if (text.empty())
		return "";

	if (hasScheme(text))
		return text;

	std::string templateToUse = searchTemplate.empty()
		? kWebSearchDuckDuckGo : searchTemplate;

	if (looksLikeHost(text)) {
		std::string url = "http://" + text;
		return url;
	}

	// Search. Substitute %s with the encoded query; a template without
	// %s gets the query appended.
	std::string encoded = web_url_encode(text);
	size_t at = templateToUse.find("%s");
	if (at == std::string::npos)
		return templateToUse + encoded;
	return templateToUse.substr(0, at) + encoded
		+ templateToUse.substr(at + 2);
}
