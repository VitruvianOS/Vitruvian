/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARE_LIST_ITEM_H
#define _SHARE_LIST_ITEM_H


#include <ListItem.h>
#include <String.h>

#include "ShareCore.h"


// One configured share plus live mount state for the list.
struct ShareListEntry {
	VosNetworkShareSpec	spec;
	bool			mounted;
	bool			unreachable;

	ShareListEntry()
		:
		mounted(false),
		unreachable(false)
	{
	}
};


class ShareListItem : public BStringItem {
public:
					ShareListItem(const ShareListEntry& entry);

			void	UpdateItemText();
			const ShareListEntry& Entry() const { return fEntry; }
			ShareListEntry& EntryRef() { return fEntry; }

private:
			ShareListEntry	fEntry;
};


#endif	// _SHARE_LIST_ITEM_H
