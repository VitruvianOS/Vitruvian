/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef WEB_BOOKMARKS_H
#define WEB_BOOKMARKS_H


#include <SupportDefs.h>

#include <string>
#include <vector>


// Bookmarks stored as a Netscape-format HTML file (the parser in the
// kit's private sources reads and writes it). The file is a plain
// document Tracker can show; no node attributes are used.
class BWebBookmarks {
public:
								BWebBookmarks();
								~BWebBookmarks();

			status_t			Load(const char* path);
			status_t			Save() const;

			// Adding an URL that is already bookmarked replaces
			// its title.
			void				Add(const char* title, const char* url);
			bool				Remove(const char* url);
			bool				Contains(const char* url) const;

			int32				Count() const { return (int32)fItems.size(); }
			const char*			TitleAt(int32 index) const;
			const char*			URLAt(int32 index) const;

private:
	struct	Item {
		std::string	title;
		std::string	url;
	};

	std::vector<Item>	fItems;
			std::string			fPath;
};


#endif // WEB_BOOKMARKS_H
