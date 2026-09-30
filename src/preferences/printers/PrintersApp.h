/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTERS_APP_H
#define _PRINTERS_APP_H


#include <Application.h>

#include "Messages.h"


class PrintersApp : public BApplication {
public:
						PrintersApp();

	virtual void		ReadyToRun();
	virtual void		MessageReceived(BMessage* message);
};


#endif // _PRINTERS_APP_H
