/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PLAYLIST_ITEM_H
#define _PLAYLIST_ITEM_H


#include <Entry.h>
#include <String.h>
#include <Url.h>


class PlaylistItem {
public:
								PlaylistItem(const entry_ref& ref);
	virtual						~PlaylistItem();

			const entry_ref&	Ref() const
									{ return fRef; }
			const BString&		Name() const
									{ return fName; }
			const BUrl&			Url() const
									{ return fUrl; }

private:
			entry_ref			fRef;
			BString				fName;
			BUrl				fUrl;
};


#endif // _PLAYLIST_ITEM_H
