/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BluetoothStatusWindow.h"
#include "BluetoothStatusView.h"

#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <LayoutBuilder.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothStatusWindow"


BluetoothStatusWindow::BluetoothStatusWindow()
	:
	BWindow(BRect(100, 100, 300, 200), B_TRANSLATE("Bluetooth"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
{
	BluetoothStatusView* view = new BluetoothStatusView(Bounds(), B_FOLLOW_ALL, false);
	AddChild(view);
	
	CenterOnScreen();
}


BluetoothStatusWindow::~BluetoothStatusWindow()
{
}


bool
BluetoothStatusWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}