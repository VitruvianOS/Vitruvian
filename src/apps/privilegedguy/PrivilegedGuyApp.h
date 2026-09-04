/*
 * Copyright 2026, Vitruvian. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PRIVILEGED_GUY_APP_H
#define PRIVILEGED_GUY_APP_H


#include <Application.h>


class PrivilegedGuyApp : public BApplication {
public:
								PrivilegedGuyApp();

	virtual	void				ReadyToRun();
};

#endif // PRIVILEGED_GUY_APP_H
