/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _URL_INPUT_H
#define _URL_INPUT_H


#include <stdint.h>
#include <string>


enum UrlInputKind {
	kUrlInputNone,
		// empty input
	kUrlInputUrl,
		// navigable as typed or after adding https://
	kUrlInputSearch,
		// hand to the search engine
};


struct SearchEngine {
	const char*	name;
	const char*	urlTemplate;
		// %s stands for the percent-encoded query
};


int32_t				CountSearchEngines();
const SearchEngine&	SearchEngineAt(int32_t index);


// Classifies bar input the way browsers do: a scheme (known or with
// //) or a recognizable host.tld shape is a URL, anything else is a
// search query. Canonical URLs come back with the scheme added.
UrlInputKind		ClassifyUrlInput(const char* text,
						std::string* canonicalUrl);


// Builds the engine's search URL for query, percent-encoded.
std::string			BuildSearchURL(const char* query, int32_t engine);


#endif // _URL_INPUT_H
