/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MAIN_WIN_H
#define _MAIN_WIN_H


#include <Entry.h>
#include <Window.h>


class Controller;
class ControllerView;
class Playlist;
class PlaylistWindow;
class VideoView;
class BMenuBar;
class BMenu;
class BFilePanel;


class MainWin : public BWindow {
public:
								MainWin(BRect frame);
	virtual						~MainWin();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				DispatchMessage(BMessage* message, BHandler* handler);
	virtual	void				FrameResized(float newWidth, float newHeight);
	virtual	bool				QuitRequested();

private:
			void				_SetupWindow();
			void				_CreateMenu();
			void				_OpenRefs(BMessage* message);
			void				_OpenLocation();
			void				_OpenMidiRef(const entry_ref& ref);
			void				_PlayCd();
			void				_ShowPlaylist();
			void				_UpdateTransport();
			void				_UpdateTitle(const char* name);
			void				_SizeForVideo(uint32 width, uint32 height);
			void				_PollPosition();

private:
			Controller*			fController;
			Playlist*			fPlaylist;
			VideoView*			fVideoView;
			ControllerView*		fControllerView;
			PlaylistWindow*		fPlaylistWindow;
			BMenuBar*			fMenuBar;
			BFilePanel*			fOpenPanel;
			BMessageRunner*		fPositionRunner;
			bool				fMuted;
};


#endif // _MAIN_WIN_H
