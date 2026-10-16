/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _SUGGEST_MODEL_H
#define _SUGGEST_MODEL_H


#include <stdint.h>
#include <string>
#include <vector>


struct SuggestMatch {
	std::string	text;
		// the history entry as stored
	int32_t		historyIndex;
};


/*!	Autocomplete matches for the URL bar: history entries that
	prefix-match the input first, substring matches after, most
	recent first inside each group. Plain C++ so a host test covers
	the ranking without BViews.
*/
class SuggestModel {
public:
	static const int32_t kMaxMatches = 8;

			int32_t			Update(const char* input,
								const std::vector<std::string>& history);

			int32_t			Count() const;
			const SuggestMatch&	MatchAt(int32_t index) const;

private:
			std::vector<SuggestMatch>	fMatches;
};


#endif // _SUGGEST_MODEL_H
