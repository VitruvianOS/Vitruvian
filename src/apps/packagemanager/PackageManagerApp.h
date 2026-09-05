/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_MANAGER_APP_H
#define PACKAGE_MANAGER_APP_H

#include <Application.h>


class MainWindow;


class PackageManagerApp : public BApplication {
	typedef BApplication Inherited;
public:
								PackageManagerApp();
	virtual						~PackageManagerApp();

	virtual	void				ReadyToRun();
	virtual	bool				QuitRequested();

private:
			MainWindow*			fWindow;
};


#endif // PACKAGE_MANAGER_APP_H
