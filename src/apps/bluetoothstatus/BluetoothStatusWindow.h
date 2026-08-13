/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef BLUETOOTH_STATUS_WINDOW_H
#define BLUETOOTH_STATUS_WINDOW_H


#include <Window.h>


class BluetoothStatusView;


class BluetoothStatusWindow : public BWindow {
public:
						BluetoothStatusWindow();
	virtual				~BluetoothStatusWindow();

	virtual	bool		QuitRequested();
};


#endif	// BLUETOOTH_STATUS_WINDOW_H