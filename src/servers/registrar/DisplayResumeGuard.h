/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DISPLAY_RESUME_GUARD_H
#define _DISPLAY_RESUME_GUARD_H


#include <String.h>


// Knows which DRM display drivers cannot resume from sleep. The registrar
// owns one instance and uses it to gate suspend and hibernate.
class DisplayResumeGuard {
public:
					DisplayResumeGuard();

	// Finds the active display's card under sysfsRoot (NULL: /sys/class/drm)
	// in app_server's order; false if no card has a driver.
	bool			Detect(const char* sysfsRoot = NULL);

	// Kernel driver name of the active display card; empty if unknown.
	const BString&	DriverName() const { return fDriver; }

	// True when this driver is known not to bring the display back.
	bool			GuardsSuspend() const;
	bool			GuardsHibernate() const;

private:
	static bool		_ReadDriver(const char* cardPath, BString& driver);
	static bool		_HasConnectedConnector(const char* cardPath);
	static bool		_IsBootVga(const char* cardPath);

	BString			fDriver;
};


#endif	// _DISPLAY_RESUME_GUARD_H
