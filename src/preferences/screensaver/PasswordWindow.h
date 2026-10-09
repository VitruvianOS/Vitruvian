/*
 * Copyright 2003-2013 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Phipps
 *		Jérôme Duval, jerome.duval@free.fr
 *		Dario Casalinuovo
 */
#ifndef PASSWORD_WINDOW_H
#define PASSWORD_WINDOW_H


#include <Window.h>


class ScreenSaverSettings;


class PasswordWindow : public BWindow {
public:
								PasswordWindow(ScreenSaverSettings& settings);

	virtual	void				MessageReceived(BMessage* message);

			void				Update();

private:
			void				_Setup();

			ScreenSaverSettings& fSettings;
};


#endif	// PASSWORD_WINDOW_H
