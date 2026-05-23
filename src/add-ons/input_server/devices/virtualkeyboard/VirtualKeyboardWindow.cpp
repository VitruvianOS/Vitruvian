/*
 * Copyright 2014 Freeman Lou <freemanlou2430@yahoo.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "VirtualKeyboardWindow.h"

#include <GroupLayoutBuilder.h>
#include <Screen.h>

#include "KeyboardLayoutView.h"


VirtualKeyboardWindow::VirtualKeyboardWindow(BInputServerDevice* dev)
	:
	BWindow(BRect(0,0,0,0),"Virtual Keyboard",
	B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
	B_WILL_ACCEPT_FIRST_CLICK | B_AVOID_FOCUS),
	fDevice(dev)
{
	BScreen screen;
	BRect screenRect(screen.Frame());

	ResizeTo(screenRect.Width(), screenRect.Height() / 3);
	MoveTo(0,screenRect.Height() - screenRect.Height() / 3);

	SetLayout(new BGroupLayout(B_VERTICAL));

	fCurrentKeymap.SetToCurrent();

	fKeyboardView = new KeyboardLayoutView("Keyboard",fDevice);
	fKeyboardView->GetKeyboardLayout()->SetDefault();
	fKeyboardView->SetEditable(false);
	fKeyboardView->SetKeymap(&fCurrentKeymap);

	AddChild(BGroupLayoutBuilder(B_VERTICAL)
		.Add(fKeyboardView));
}


void
VirtualKeyboardWindow::MessageReceived(BMessage* message)
{
	BWindow::MessageReceived(message);
}
