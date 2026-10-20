/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EXPAND_VOLUME_APP_H
#define _EXPAND_VOLUME_APP_H

#include <Application.h>
#include <String.h>


// TryNow popup: grow the system partition into the adjacent free space.
// One window, no servers; degrades to an explanatory no-op pane when the
// volume cannot be read or no space is claimable.
class ExpandVolumeApp : public BApplication {
public:
							ExpandVolumeApp();

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			ReadyToRun();
	virtual	bool			QuitRequested();

	static	const char*		Signature();
};


// Per-home "do not nag again" marker, written when the user is done with
// the popup (success or dismissal). The first_login launcher checks it.
status_t
WriteExpandVolumeDoneMarker();

bool
ExpandVolumeDoneMarkerExists();


#endif	// _EXPAND_VOLUME_APP_H
