/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CONTROLLER_VIEW_H
#define _CONTROLLER_VIEW_H


#include "TransportControlGroup.h"


class Controller;
class Playlist;


class ControllerView : public TransportControlGroup {
public:
								ControllerView(BRect frame, Controller* controller,
									Playlist* playlist);
	virtual						~ControllerView();

	// TransportControlGroup
	virtual	void				TogglePlaying();
	virtual	void				Stop();
	virtual	void				Rewind();
	virtual	void				Forward();
	virtual	void				SkipBackward();
	virtual	void				SkipForward();
	virtual	void				VolumeChanged(float value);
	virtual	void				ToggleMute();
	virtual	void				PositionChanged(float value);
	virtual	bigtime_t			TimePositionFor(float value);

private:
			void				_CheckSkippable();

private:
			Controller*			fController;
			Playlist*			fPlaylist;
};


#endif // _CONTROLLER_VIEW_H
