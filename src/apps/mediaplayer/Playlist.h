/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PLAYLIST_H
#define _PLAYLIST_H


#include <List.h>
#include <Locker.h>
#include <Message.h>
#include <ObjectList.h>
#include <String.h>

#include "PlaylistItem.h"


class Playlist : public BLocker {
public:
	class Listener {
	public:
		virtual					~Listener() {}
		virtual	void			ItemsChanged() {}
		virtual	void			CurrentItemChanged(int32 newIndex, bool play) {}
	};

								Playlist();
								~Playlist();

			int32				CountItems() const;
			PlaylistItem*		ItemAt(int32 index) const;
			PlaylistItem*		CurrentItem() const;
			int32				CurrentItemIndex() const
									{ return fCurrentIndex; }

			bool				AddItem(const entry_ref& ref);
			bool				AddItems(const BMessage* refsReceivedMessage);
			bool				RemoveItem(int32 index);
			void				RemoveItems(int32 fromIndex, int32 toIndex);
			void				Clear();

			bool				SetCurrentItemIndex(int32 index, bool play);
			PlaylistItem*		Advance(bool forward, bool loop);

			void				AddListener(Listener* listener);
			void				RemoveListener(Listener* listener);

private:
			void				_NotifyItemsChanged();
			void				_NotifyCurrentItemChanged(bool play);

private:
			BObjectList<PlaylistItem>	fItems;
			BList						fListeners;
			int32						fCurrentIndex;
};


#endif // _PLAYLIST_H
