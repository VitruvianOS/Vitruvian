/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DisplayChangeTracker.h"


DisplayChangeTracker::DisplayChangeTracker()
	:
	fPending(false),
	fOwner(-1),
	fGeneration(0)
{
}


uint32_t
DisplayChangeTracker::Begin(int32_t owner)
{
	if (!fPending) {
		fPending = true;
		fOwner = owner;
	}

	fGeneration++;
	return fGeneration;
}


bool
DisplayChangeTracker::Confirm(int32_t owner)
{
	if (!fPending || fOwner != owner)
		return false;

	fPending = false;
	return true;
}


bool
DisplayChangeTracker::Cancel(int32_t owner)
{
	if (!fPending || fOwner != owner)
		return false;

	fPending = false;
	return true;
}


bool
DisplayChangeTracker::Timeout(uint32_t generation)
{
	if (!fPending || fGeneration != generation)
		return false;

	fPending = false;
	return true;
}


bool
DisplayChangeTracker::OwnerDied(int32_t owner)
{
	if (!fPending || fOwner != owner)
		return false;

	fPending = false;
	return true;
}
