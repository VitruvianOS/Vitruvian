/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <web/WebBookmarks.h>

#include <fstream>

#include "bookmarks_parser.h"


BWebBookmarks::BWebBookmarks()
{
}


BWebBookmarks::~BWebBookmarks()
{
}


status_t
BWebBookmarks::Load(const char* path)
{
	fPath = path != NULL ? path : "";
	fItems.clear();

	std::ifstream in(fPath.c_str());
	if (!in.good())
		return B_ENTRY_NOT_FOUND;

	std::vector<BookmarkItem> parsed = ParseNetscapeBookmarks(in);
	for (const auto& item : parsed) {
		Item entry;
		entry.title = item.title;
		entry.url = item.url;
		fItems.push_back(entry);
	}
	return B_OK;
}


status_t
BWebBookmarks::Save() const
{
	if (fPath.empty())
		return B_BAD_VALUE;

	std::ofstream out(fPath.c_str(), std::ios::trunc);
	if (!out.good())
		return B_ERROR;

	std::vector<BookmarkItem> items;
	items.reserve(fItems.size());
	for (const auto& item : fItems) {
		BookmarkItem entry;
		entry.title = item.title;
		entry.url = item.url;
		items.push_back(entry);
	}
	SerializeNetscapeBookmarks(out, items);
	return B_OK;
}


void
BWebBookmarks::SetPath(const char* path)
{
	fPath = path != NULL ? path : "";
}


void
BWebBookmarks::Add(const char* title, const char* url)
{
	if (url == NULL || url[0] == '\0')
		return;

	for (auto& item : fItems) {
		if (item.url == url) {
			if (title != NULL)
				item.title = title;
			return;
		}
	}

	Item item;
	item.url = url;
	item.title = title != NULL ? title : url;
	fItems.push_back(item);
}


bool
BWebBookmarks::Remove(const char* url)
{
	if (url == NULL)
		return false;

	for (auto it = fItems.begin(); it != fItems.end(); ++it) {
		if (it->url == url) {
			fItems.erase(it);
			return true;
		}
	}
	return false;
}


bool
BWebBookmarks::Contains(const char* url) const
{
	if (url == NULL)
		return false;

	for (const auto& item : fItems) {
		if (item.url == url)
			return true;
	}
	return false;
}


const char*
BWebBookmarks::TitleAt(int32 index) const
{
	if (index < 0 || index >= (int32)fItems.size())
		return NULL;
	return fItems[index].title.c_str();
}


const char*
BWebBookmarks::URLAt(int32 index) const
{
	if (index < 0 || index >= (int32)fItems.size())
		return NULL;
	return fItems[index].url.c_str();
}
