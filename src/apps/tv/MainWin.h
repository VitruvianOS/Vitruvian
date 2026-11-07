/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TV_MAIN_WIN_H
#define TV_MAIN_WIN_H


#include <Window.h>

#include "Controller.h"


class BAlert;
class BMenuBar;
class BMenu;
class VideoView;


class MainWin : public BWindow {
public:
								MainWin(BRect frame);
	virtual						~MainWin();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildMenu();
			void				_PopulateSourceMenu();
			void				_StartCapture();
			void				_StopCapture();

			BMenuBar*			fMenuBar;
			VideoView*			fVideoView;
			Controller			fController;
			BMenu*				fSourceMenu;
};


#endif
