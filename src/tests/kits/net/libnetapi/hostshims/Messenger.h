/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_MESSENGER_H
#define _HOSTSHIMS_MESSENGER_H

#include <Message.h>
#include <SupportDefs.h>

class BMessenger {
public:
	BMessenger() : fTarget(NULL), fCookie(NULL) {}
	BMessenger(void (*target)(void*, BMessage*), void* cookie)
		: fTarget(target), fCookie(cookie) {}
	BMessenger(const BMessenger& other)
		: fTarget(other.fTarget), fCookie(other.fCookie) {}

	BMessenger& operator=(const BMessenger& other)
	{
		if (this != &other) {
			fTarget = other.fTarget;
			fCookie = other.fCookie;
		}
		return *this;
	}

	bool IsValid() const { return fTarget != NULL; }

	status_t SendMessage(BMessage* message) const
	{
		if (fTarget == NULL)
			return B_BAD_VALUE;
		if (message != NULL)
			fTarget(fCookie, message);
		return B_OK;
	}

	bool operator==(const BMessenger& other) const
	{
		return fTarget == other.fTarget && fCookie == other.fCookie;
	}

private:
	void (*fTarget)(void*, BMessage*);
	void* fCookie;
};

#endif
