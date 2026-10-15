/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MAIN_WIN_H
#define _MAIN_WIN_H


#include <Entry.h>
#include <Messenger.h>
#include <Rect.h>
#include <Window.h>

#include "FullScreenState.h"


class Controller;
class ControllerView;
class InfoWin;
class LocationStringView;
class Playlist;
class PlaylistWindow;
class SettingsMessage;
class VideoView;
class BHandler;
class BMenuBar;
class BMenu;
class BMessage;
class BFilePanel;
class BMenuItem;


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
			void				_UpdateLocation();
			void				_UpdateVideoMode();
			void				_SizeForVideo(uint32 width, uint32 height);
			void				_PollPosition();
			void				_SeekRelative(bigtime_t delta);
			bool				_ShouldHandleKey(BHandler* handler);
			void				_ToggleFullscreen();
			void				_EnterFullscreen();
			void				_ExitFullscreen();
			void				_ApplyFullScreenResult(
									const FullScreenState::Result& result);
			void				_SetCursorHidden(bool hidden);
			void				_KeepAspectOnResize();
			void				_PositionControls();
			void				_ShowControls();
			void				_HideControlsIdle();
			void				_ToggleHideInterface();
			void				_SetAlwaysOnTop(bool on);
			void				_SetLoop(bool on);
			void				_ShowInfo();
			void				_QuitInfoWin();
			void				_Zoom(float factor);
			void				_FitToScreen();
			void				_LoadSettings();
			void				_SaveSettings();
			float				_ChromeHeight() const;

private:
			Controller*			fController;
			Playlist*			fPlaylist;
			VideoView*			fVideoView;
			ControllerView*		fControllerView;
			LocationStringView*	fLocationView;
			PlaylistWindow*		fPlaylistWindow;
			// The info window deletes itself on close; the messenger
			// reports when that happened instead of a raw pointer.
			BMessenger			fInfoMessenger;
			BMenuBar*			fMenuBar;
			BFilePanel*			fOpenPanel;
			BMessageRunner*		fPositionRunner;
			BMenuItem*			fAlwaysOnTopItem;
			BMenuItem*			fHideInterfaceItem;
			BMenuItem*			fLoopItem;
			SettingsMessage*		fSettings;
			bool				fMuted;
			bool				fLoop;
			bool				fAlwaysOnTop;
			bool				fHideInterface;
			FullScreenState		fFullScreen;
			bool				fCursorHidden;
			float				fVideoScale;
			int32				fStripLayoutIndex;
			float				fStripHeight;
			float				fControlY;
			uint32				fVideoParNum;
			uint32				fVideoParDen;
			uint32				fVideoRotation;
			BRect				fSavedFrame;
			BRect				fLastFrame;
};


#endif // _MAIN_WIN_H
