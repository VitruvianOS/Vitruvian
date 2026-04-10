/*
 * Copyright 2024, Vitruvian OS.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_APP_H
#define INSTALLER_APP_H


#include <Application.h>

#include "InstallerWindow.h"


class InstallerApp : public BApplication {
public:
								InstallerApp();

	virtual	void				MessageReceived(BMessage* message);

	virtual	void				ReadyToRun();
	virtual	void				Quit();
};

#endif // INSTALLER_APP_H
