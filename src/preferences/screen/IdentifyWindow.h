/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef IDENTIFY_WINDOW_H
#define IDENTIFY_WINDOW_H


#include <MessageRunner.h>
#include <String.h>
#include <Window.h>


// A big number and the connector name on one screen, for a moment.
class IdentifyWindow : public BWindow {
public:
							IdentifyWindow(BRect screenFrame, int32 number,
								const char* name);
	virtual					~IdentifyWindow();

	virtual	void			MessageReceived(BMessage* message);

private:
			BMessageRunner*	fRunner;
};


#endif	// IDENTIFY_WINDOW_H
