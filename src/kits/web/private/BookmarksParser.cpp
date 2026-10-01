/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "bookmarks_parser.h"


std::vector<BookmarkItem>
ParseNetscapeBookmarks(std::istream& in)
{
	std::vector<BookmarkItem> result;
	std::string line;

	while (std::getline(in, line)) {
		// Match: <DT><A HREF="url">title</A>
		size_t hrefPos = line.find("<DT><A HREF=\"");
		if (hrefPos == std::string::npos)
			continue;

		size_t urlStart = hrefPos + 13;  // len("<DT><A HREF=\"")
		size_t urlEnd   = line.find('"', urlStart);
		if (urlEnd == std::string::npos) continue;

		size_t gtPos    = line.find('>', urlEnd);
		size_t closePos = line.find("</A>", gtPos);
		if (gtPos == std::string::npos || closePos == std::string::npos)
			continue;

		BookmarkItem item;
		item.url   = line.substr(urlStart, urlEnd - urlStart);
		item.title = line.substr(gtPos + 1, closePos - gtPos - 1);
		result.push_back(item);
	}

	return result;
}


void
SerializeNetscapeBookmarks(std::ostream& out,
	const std::vector<BookmarkItem>& items)
{
	out << "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
		<< "<META HTTP-EQUIV=\"Content-Type\" "
		<< "CONTENT=\"text/html; charset=UTF-8\">\n"
		<< "<TITLE>Bookmarks</TITLE>\n"
		<< "<H1>Bookmarks</H1>\n"
		<< "<DL><p>\n";

	for (const auto& bm : items)
		out << "    <DT><A HREF=\"" << bm.url << "\">" << bm.title << "</A>\n";

	out << "</DL><p>\n";
}
