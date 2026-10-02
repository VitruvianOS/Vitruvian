/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_APP_H
#define SERVICES_APP_H

#include <Application.h>

#include "ServicesDefs.h"


class ServicesWindow;


class ServicesApp : public BApplication {
	typedef BApplication Inherited;
public:
								ServicesApp();

	virtual	void				ReadyToRun();
	virtual	bool				QuitRequested();

private:
			ServicesWindow*		fWindow;
};


#endif // SERVICES_APP_H
