/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_APP_H
#define _CORTEX_APP_H


#include <Application.h>


class CortexApp : public BApplication {
public:
								CortexApp();

	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				ReadyToRun();

private:
			bool				fDumpRequested;
};


#endif
