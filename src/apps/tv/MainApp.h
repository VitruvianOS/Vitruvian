/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TV_MAIN_APP_H
#define TV_MAIN_APP_H


#include <Application.h>


class MainWin;


class MainApp : public BApplication {
public:
								MainApp();
	virtual						~MainApp();

private:
			MainWin*			fMainWindow;
};


#endif
