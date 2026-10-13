/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi2/MidiEndpoint.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>

#include <String.h>

#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>
#include <media2/MidiUnit.h>


BMidiEndpoint::BMidiEndpoint(const char *name)
{
	fId = 0;
	fName.SetTo(name != NULL ? name : "");
	fRefCount = 1;
	fProperties = NULL;
	fIsLocal = false;
	fIsConsumer = false;
	fIsRegistered = false;
	fIsAlive = true;
	fUnit = NULL;
	memset(_reserved, 0, sizeof(_reserved));
}


BMidiEndpoint::~BMidiEndpoint()
{
	delete fProperties;
	fProperties = NULL;
	if (fUnit != NULL) {
		fUnit->Stop();
		delete fUnit;
		fUnit = NULL;
	}
}


const char *
BMidiEndpoint::Name() const
{
	return fName.String();
}


void
BMidiEndpoint::SetName(const char *name)
{
	if (name == NULL)
		return;
	fName.SetTo(name);
	if (fUnit != NULL) {
		// PW_KEY_NODE_DESCRIPTION is set at filter creation; a live rename
		// needs a graph round-trip. Store the name for the next Start().
	}
}


int32
BMidiEndpoint::ID() const
{
	return fId;
}


bool
BMidiEndpoint::IsProducer() const
{
	return !fIsConsumer;
}


bool
BMidiEndpoint::IsConsumer() const
{
	return fIsConsumer;
}


bool
BMidiEndpoint::IsRemote() const
{
	return !fIsLocal;
}


bool
BMidiEndpoint::IsLocal() const
{
	return fIsLocal;
}


bool
BMidiEndpoint::IsPersistent() const
{
	// Documented upstream: always false.
	return false;
}


bool
BMidiEndpoint::IsValid() const
{
	return fIsAlive && fUnit != NULL;
}


status_t
BMidiEndpoint::Release()
{
	if (fRefCount <= 0)
		return B_BAD_VALUE;
	fRefCount--;
	return B_OK;
}


status_t
BMidiEndpoint::Acquire()
{
	if (!fIsAlive)
		return B_BAD_VALUE;
	fRefCount++;
	return B_OK;
}


status_t
BMidiEndpoint::SetProperties(const BMessage *properties)
{
	if (properties == NULL)
		return B_BAD_VALUE;
	delete fProperties;
	fProperties = new BMessage(*properties);
	if (fProperties == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


status_t
BMidiEndpoint::GetProperties(BMessage *properties) const
{
	if (properties == NULL)
		return B_BAD_VALUE;
	if (fProperties == NULL)
		return B_ENTRY_NOT_FOUND;
	*properties = *fProperties;
	return B_OK;
}


status_t
BMidiEndpoint::Register()
{
	if (fUnit == NULL)
		return B_NO_INIT;
	fIsRegistered = true;
	BMidiRoster::Register(this);
	return B_OK;
}


status_t
BMidiEndpoint::Unregister()
{
	if (fUnit == NULL)
		return B_NO_INIT;
	fIsRegistered = false;
	BMidiRoster::Unregister(this);
	return B_OK;
}


status_t
BMidiEndpoint::SendRegisterRequest(bool)
{
	return B_OK;
}


status_t
BMidiEndpoint::SendChangeRequest(BMessage *)
{
	return B_OK;
}


bool
BMidiEndpoint::IsRegistered() const
{
	return fIsRegistered;
}


bool
BMidiEndpoint::LockLooper() const
{
	return true;
}


void
BMidiEndpoint::UnlockLooper() const
{
}


void
BMidiEndpoint::_Reserved1()
{
}


void
BMidiEndpoint::_Reserved2()
{
}


void
BMidiEndpoint::_Reserved3()
{
}


void
BMidiEndpoint::_Reserved4()
{
}


void
BMidiEndpoint::_Reserved5()
{
}


void
BMidiEndpoint::_Reserved6()
{
}


void
BMidiEndpoint::_Reserved7()
{
}


void
BMidiEndpoint::_Reserved8()
{
}
