/*
 * Copyright 2014 Freeman Lou <freemanlou2430@yahoo.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef VIRTUAL_KEYBOARD_WINDOW_H
#define VIRTUAL_KEYBOARD_WINDOW_H

#include <InputServerDevice.h>
#include <Window.h>

#include "Keymap.h"

class KeyboardLayoutView;

class VirtualKeyboardWindow : public BWindow{
public:
							VirtualKeyboardWindow(BInputServerDevice* dev);
		virtual void		MessageReceived(BMessage* message);			

private:
		KeyboardLayoutView* fKeyboardView;
		Keymap				fCurrentKeymap;
		BInputServerDevice*	fDevice;
};

#endif // VIRTUAL_KEYBOARD_WINDOW_H
