/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _INFO_WIN_H
#define _INFO_WIN_H


#include <Entry.h>
#include <Window.h>


class BStringView;
class Controller;


// File Info window: shows the probed media details for the current item in
// the shape of the legacy player's Info window.
class InfoWin : public BWindow {
public:
								InfoWin(BRect frame, Controller* controller,
									const entry_ref& ref);
	virtual						~InfoWin();

private:
			void				_Setup(Controller* controller,
									const entry_ref& ref);

			BStringView*		fName;
			BStringView*		fContainer;
			BStringView*		fLocation;
			BStringView*		fDuration;
			BStringView*		fVideo;
			BStringView*		fAudio;
			BStringView*		fCopyright;
};


#endif // _INFO_WIN_H
