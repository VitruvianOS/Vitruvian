/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINT_OMATIC_APP_H
#define _PRINT_OMATIC_APP_H


#include <Application.h>


class PrintOMaticWindow;


class PrintOMaticApp : public BApplication {
public:
					PrintOMaticApp();

	virtual	void	ReadyToRun();
	virtual	void	ArgvReceived(int32 argc, char** argv);
	virtual	void	RefsReceived(BMessage* message);

private:
			void	_OpenWithScanner(const char* name);

			PrintOMaticWindow*	fWindow;
};


#endif	// _PRINT_OMATIC_APP_H
