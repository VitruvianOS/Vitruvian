/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlaylistItem.h"

#include <Path.h>


PlaylistItem::PlaylistItem(const entry_ref& ref)
	:
	fRef(ref)
{
	BPath path(&ref);
	if (path.InitCheck() == B_OK) {
		fName = path.Leaf();
		fUrl = BUrl(path);
	} else {
		fName = ref.name;
		fUrl.SetUrlString(fName, false);
	}
}


PlaylistItem::~PlaylistItem()
{
}
