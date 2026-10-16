/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "HistoryStore.h"


void
HistoryStore::Add(const char* url)
{
	if (url == NULL || url[0] == '\0')
		return;

	if (!fEntries.empty() && fEntries.front() == url)
		return;

	fEntries.insert(fEntries.begin(), url);
	while ((int32_t)fEntries.size() > kMaxEntries)
		fEntries.pop_back();
}


int32_t
HistoryStore::Count() const
{
	return (int32_t)fEntries.size();
}


const char*
HistoryStore::At(int32_t index) const
{
	if (index < 0 || index >= (int32_t)fEntries.size())
		return NULL;
	return fEntries[index].c_str();
}


const std::vector<std::string>&
HistoryStore::Entries() const
{
	return fEntries;
}
