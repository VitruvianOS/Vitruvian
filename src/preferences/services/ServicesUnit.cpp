/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ServicesUnit.h"

#include <Message.h>


ServicesUnit::ServicesUnit()
	:
	fIsService(false)
{
}


void
ServicesUnit::SetFromListTuple(const char* name, const char* description,
	const char* loadState, const char* activeState, const char* subState)
{
	fName = name != NULL ? name : "";
	fDescription = description != NULL ? description : "";
	fLoadState = loadState != NULL ? loadState : "";
	fActiveState = activeState != NULL ? activeState : "";
	fSubState = subState != NULL ? subState : "";
	fIsService = fName.IEndsWith(".service");
}


bool
ServicesUnit::MatchesFilter(unit_filter filter) const
{
	if (filter == kFilterAllUnits)
		return true;
	return fIsService;
}


bool
ServicesUnit::MatchesText(const BString& text) const
{
	if (text.Length() == 0)
		return true;
	return fName.IFindFirst(text) >= 0
		|| fDescription.IFindFirst(text) >= 0;
}


void
ServicesUnit::AddTo(BMessage* message, const char* prefix) const
{
	if (message == NULL)
		return;

	BString key;
	key << prefix << "/name";
	message->AddString(key.String(), fName.String());

	key = prefix;
	key << "/description";
	message->AddString(key.String(), fDescription.String());

	key = prefix;
	key << "/load";
	message->AddString(key.String(), fLoadState.String());

	key = prefix;
	key << "/active";
	message->AddString(key.String(), fActiveState.String());

	key = prefix;
	key << "/sub";
	message->AddString(key.String(), fSubState.String());

	key = prefix;
	key << "/file";
	message->AddString(key.String(), fUnitFileState.String());

	key = prefix;
	key << "/service";
	message->AddBool(key.String(), fIsService);
}


bool
ServicesUnit::FromMessage(BMessage* message, ServicesUnit* into,
	const char* prefix)
{
	if (message == NULL || into == NULL)
		return false;

	BString key;
	const char* text = NULL;

	key = prefix;
	key << "/name";
	if (message->FindString(key.String(), &text) != B_OK || text == NULL)
		return false;
	into->fName = text;

	key = prefix;
	key << "/description";
	if (message->FindString(key.String(), &text) == B_OK && text != NULL)
		into->fDescription = text;

	key = prefix;
	key << "/load";
	if (message->FindString(key.String(), &text) == B_OK && text != NULL)
		into->fLoadState = text;

	key = prefix;
	key << "/active";
	if (message->FindString(key.String(), &text) == B_OK && text != NULL)
		into->fActiveState = text;

	key = prefix;
	key << "/sub";
	if (message->FindString(key.String(), &text) == B_OK && text != NULL)
		into->fSubState = text;

	key = prefix;
	key << "/file";
	if (message->FindString(key.String(), &text) == B_OK && text != NULL)
		into->fUnitFileState = text;

	key = prefix;
	key << "/service";
	bool isService = false;
	if (message->FindBool(key.String(), &isService) == B_OK)
		into->fIsService = isService;
	else
		into->fIsService = into->fName.IEndsWith(".service");

	return true;
}
