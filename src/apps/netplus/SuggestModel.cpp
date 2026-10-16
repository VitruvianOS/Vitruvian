/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "SuggestModel.h"

#include <ctype.h>


static std::string
_Lower(const std::string& text)
{
	std::string lowered;
	lowered.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++)
		lowered += (char)tolower((unsigned char)text[i]);
	return lowered;
}


int32_t
SuggestModel::Update(const char* input,
	const std::vector<std::string>& history)
{
	fMatches.clear();

	std::string needle = input == NULL ? std::string() : _Lower(input);
	if (needle.empty())
		return 0;

	std::vector<SuggestMatch> substring;
	for (size_t i = 0; i < history.size(); i++) {
		std::string entry = history[i];
		std::string lowered = _Lower(entry);

		// The typed text itself is not a suggestion.
		if (lowered == needle)
			continue;

		SuggestMatch match;
		match.text = entry;
		match.historyIndex = (int32_t)i;

		if (lowered.compare(0, needle.size(), needle) == 0)
			fMatches.push_back(match);
		else if (lowered.find(needle) != std::string::npos)
			substring.push_back(match);
	}

	for (size_t i = 0; i < substring.size()
		&& (int32_t)fMatches.size() < kMaxMatches; i++) {
		fMatches.push_back(substring[i]);
	}

	if ((int32_t)fMatches.size() > kMaxMatches)
		fMatches.resize(kMaxMatches);
	return (int32_t)fMatches.size();
}


int32_t
SuggestModel::Count() const
{
	return (int32_t)fMatches.size();
}


const SuggestMatch&
SuggestModel::MatchAt(int32_t index) const
{
	static const SuggestMatch kEmpty = { std::string(), 0 };
	if (index < 0 || index >= (int32_t)fMatches.size())
		return kEmpty;
	return fMatches[index];
}
