/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_UNIT_H
#define SERVICES_UNIT_H

#include <String.h>

#include "ServicesDefs.h"


class BMessage;


// One systemd unit as seen over D-Bus. The list view holds one of these
// per row; the worker copies them through BMessages.
struct ServicesUnit {
	BString	fName;
	BString	fDescription;
	BString	fLoadState;
	BString	fActiveState;
	BString	fSubState;
	BString	fUnitFileState;
	bool	fIsService;

			ServicesUnit();

	bool			MatchesFilter(unit_filter filter) const;
	bool			MatchesText(const BString& text) const;

			// name may be NULL for any missing field.
	void			SetFromListTuple(const char* name, const char* description,
								const char* loadState, const char* activeState,
								const char* subState);

			// Appends fields under "<prefix>/name" etc.
	void			AddTo(BMessage* message, const char* prefix = "unit") const;
	static bool		FromMessage(BMessage* message, ServicesUnit* into,
								const char* prefix = "unit");
};


#endif // SERVICES_UNIT_H
