/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SLEEP_WINDOW_H
#define SLEEP_WINDOW_H


#include <Alert.h>


static const uint32 kMsgSleepFailed = 'slFl';


// "Suspending..."/"Hibernating..." while the system sleeps. B_QUIT_REQUESTED closes it and
// kMsgSleepFailed ("text") turns it into a dismissable error.
class SleepWindow : public BAlert {
public:
								SleepWindow(bool hibernate);

	virtual	void				MessageReceived(BMessage* message);
};


#endif	// SLEEP_WINDOW_H
