/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _HISTORY_STORE_H
#define _HISTORY_STORE_H


#include <stdint.h>
#include <string>
#include <vector>


/*!	Session history: most recent first, no consecutive duplicates,
	capped. Plain C++ so the autocomplete model and host tests can
	read it without BViews.
*/
class HistoryStore {
public:
	static const int32_t kMaxEntries = 24;

			void			Add(const char* url);

			int32_t			Count() const;
			const char*		At(int32_t index) const;

			const std::vector<std::string>&	Entries() const;

private:
			std::vector<std::string>	fEntries;
};


#endif // _HISTORY_STORE_H
