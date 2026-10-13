/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_URL_NORMALIZE_H
#define WEB_URL_NORMALIZE_H


#include <string>


// Turns what the user typed in the address bar into a URL to load.
//
// - "example.com", "www.example.com/path" get http:// prepended when no
//   scheme is present.
// - Anything with a scheme (http, https, vos-about, file, ...) passes
//   through unchanged.
// - Text that does not look like a host (contains spaces, is empty after
//   trimming, or has no dot and is not localhost) becomes a search using
//   the given template, where "%s" is replaced by the url-encoded query.
//
// Pure function: no BeAPI, no WebKit; unit-tested in src/tests.
std::string
web_url_for_user_input(const std::string& input,
	const std::string& searchTemplate);

// Percent-encodes a string for use in a query component.
std::string
web_url_encode(const std::string& value);

// The built-in search engine templates. DuckDuckGo is the default.
extern const char* const kWebSearchDuckDuckGo;
extern const char* const kWebSearchStartpage;
extern const char* const kWebSearchBrave;
extern const char* const kWebSearchGoogle;
extern const char* const kWebSearchBing;
extern const char* const kWebSearchWikipedia;


#endif // WEB_URL_NORMALIZE_H
