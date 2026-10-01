/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>


struct BookmarkItem {
	std::string title;
	std::string url;
};

// Parse Netscape-HTML bookmark format (as used by browsers).
std::vector<BookmarkItem> ParseNetscapeBookmarks(std::istream& in);
void SerializeNetscapeBookmarks(std::ostream& out,
	const std::vector<BookmarkItem>& items);
