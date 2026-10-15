/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _FULL_SCREEN_STATE_H
#define _FULL_SCREEN_STATE_H


#include <OS.h>


// Fullscreen decisions without a window: MainWin applies the result of
// each call and the unit test drives the same transitions.
class FullScreenState {
public:
	struct Result {
		bool	enter;
		bool	exit;
		bool	cursorHidden;
	};

								FullScreenState();

			bool				IsFullscreen() const
									{ return fFullscreen; }
			bool				ControlsVisible() const
									{ return fControlsVisible; }
			bool				CursorHidden() const
									{ return fCursorHidden; }

			Result				Toggle(bool hasVideo, bigtime_t now);
			Result				Escape(bigtime_t now);
			void				MouseMoved(bigtime_t now);
			Result				Tick(bigtime_t now);

private:
			Result				_Snapshot(bool enter, bool exit);

			bool				fFullscreen;
			bool				fControlsVisible;
			bool				fCursorHidden;
			bigtime_t			fLastRevealTime;
};


#endif // _FULL_SCREEN_STATE_H
