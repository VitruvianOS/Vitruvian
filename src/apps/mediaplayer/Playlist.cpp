/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Playlist.h"

#include <Autolock.h>
#include <Entry.h>
#include <String.h>


Playlist::Playlist()
	:
	BLocker(true),
	fCurrentIndex(-1)
{
}


Playlist::~Playlist()
{
	Clear();
}


int32
Playlist::CountItems() const
{
	return fItems.CountItems();
}


PlaylistItem*
Playlist::ItemAt(int32 index) const
{
	return fItems.ItemAt(index);
}


PlaylistItem*
Playlist::CurrentItem() const
{
	return fItems.ItemAt(fCurrentIndex);
}


bool
Playlist::AddItem(const entry_ref& ref)
{
	{
		BAutolock _(this);
		if (fItems.AddItem(new PlaylistItem(ref)) == false)
			return false;
	}
	_NotifyItemsChanged();
	return true;
}


bool
Playlist::AddItems(const BMessage* refsReceivedMessage)
{
	if (refsReceivedMessage == NULL)
		return false;

	bool added = false;
	type_code type;
	int32 count = 0;
	if (refsReceivedMessage->GetInfo("refs", &type, &count) != B_OK
			|| type != B_REF_TYPE)
		return false;

	for (int32 i = 0; i < count; i++) {
		entry_ref ref;
		if (refsReceivedMessage->FindRef("refs", i, &ref) == B_OK)
			added = AddItem(ref) || added;
	}
	return added;
}


bool
Playlist::RemoveItem(int32 index)
{
	{
		BAutolock _(this);
		PlaylistItem* item = fItems.ItemAt(index);
		if (item == NULL)
			return false;
		fItems.RemoveItem(item);
		delete item;
		if (fCurrentIndex >= fItems.CountItems())
			fCurrentIndex = fItems.CountItems() - 1;
	}
	_NotifyItemsChanged();
	return true;
}


void
Playlist::RemoveItems(int32 fromIndex, int32 toIndex)
{
	{
		BAutolock _(this);
		for (int32 i = toIndex; i >= fromIndex; i--) {
			PlaylistItem* item = fItems.ItemAt(i);
			if (item == NULL)
				continue;
			fItems.RemoveItem(item);
			delete item;
		}
		if (fCurrentIndex >= fItems.CountItems())
			fCurrentIndex = fItems.CountItems() - 1;
	}
	_NotifyItemsChanged();
}


void
Playlist::Clear()
{
	{
		BAutolock _(this);
		fItems.MakeEmpty();
		fCurrentIndex = -1;
	}
	_NotifyItemsChanged();
}


bool
Playlist::SetCurrentItemIndex(int32 index, bool play)
{
	{
		BAutolock _(this);
		if (index < -1 || index >= fItems.CountItems())
			return false;
		if (index == fCurrentIndex && play)
			return true;
		fCurrentIndex = index;
	}
	_NotifyCurrentItemChanged(play);
	return true;
}


PlaylistItem*
Playlist::Advance(bool forward, bool loop)
{
	int32 count = CountItems();
	if (count == 0)
		return NULL;

	int32 index = CurrentItemIndex();
	if (forward) {
		index++;
		if (index >= count) {
			if (!loop)
				return NULL;
			index = 0;
		}
	} else {
		index--;
		if (index < 0) {
			if (!loop)
				return NULL;
			index = count - 1;
		}
	}
	if (!SetCurrentItemIndex(index, true))
		return NULL;
	return CurrentItem();
}


void
Playlist::AddListener(Listener* listener)
{
	BAutolock _(this);
	fListeners.AddItem(listener);
}


void
Playlist::RemoveListener(Listener* listener)
{
	BAutolock _(this);
	fListeners.RemoveItem(listener);
}


void
Playlist::_NotifyItemsChanged()
{
	BAutolock _(this);
	for (int32 i = 0; i < fListeners.CountItems(); i++) {
		Listener* listener = (Listener*)fListeners.ItemAt(i);
		if (listener != NULL)
			listener->ItemsChanged();
	}
}


void
Playlist::_NotifyCurrentItemChanged(bool play)
{
	BAutolock _(this);
	int32 index = fCurrentIndex;
	for (int32 i = 0; i < fListeners.CountItems(); i++) {
		Listener* listener = (Listener*)fListeners.ItemAt(i);
		if (listener != NULL)
			listener->CurrentItemChanged(index, play);
	}
}
