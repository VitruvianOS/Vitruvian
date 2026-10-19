/*
 * Copyright 2005, Jérôme DUVAL. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <View.h>
#include <stdlib.h>
#include <string.h>

#include "PartitionMenuItem.h"

PartitionMenuItem::PartitionMenuItem(const char* name, const char* label,
		const char* menuLabel, BMessage* message, partition_id id)
	:
	BMenuItem(label, message),
	fID(id),
	// strdup(NULL) faults; callers pass NULL when the short name is unused.
	fMenuLabel(menuLabel != NULL ? strdup(menuLabel) : NULL),
	fName(name != NULL ? strdup(name) : NULL),
	fIsValidTarget(true)
{
}


PartitionMenuItem::~PartitionMenuItem()
{
	free(fMenuLabel);
	free(fName);
}


partition_id
PartitionMenuItem::ID() const
{
	return fID;
}


const char*
PartitionMenuItem::MenuLabel() const
{
	return fMenuLabel != NULL ? fMenuLabel : Label();
}


const char*
PartitionMenuItem::Name() const
{
	return fName != NULL ? fName : Label();
}


void
PartitionMenuItem::SetIsValidTarget(bool isValidTarget)
{
	fIsValidTarget = isValidTarget;
}


bool
PartitionMenuItem::IsValidTarget() const
{
	return fIsValidTarget;
}

