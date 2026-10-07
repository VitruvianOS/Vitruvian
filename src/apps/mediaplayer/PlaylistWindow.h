/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PLAYLIST_WINDOW_H
#define _PLAYLIST_WINDOW_H


#include <Window.h>


class Controller;
class BListView;
class BButton;
class Playlist;


class PlaylistWindow : public BWindow {
public:
								PlaylistWindow(BRect frame, Controller* controller,
									Playlist* playlist);
	virtual						~PlaylistWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_UpdateList();

private:
			Controller*			fController;
			Playlist*			fPlaylist;
			BListView*			fListView;
			BButton*			fAddButton;
			BButton*			fRemoveButton;
			BButton*			fClearButton;
};


#endif // _PLAYLIST_WINDOW_H
