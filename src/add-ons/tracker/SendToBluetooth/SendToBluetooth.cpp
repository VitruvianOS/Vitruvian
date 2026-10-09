/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Tracker add-on: Send to Bluetooth device.
 */

#include <ObexSendChooser.h>

#include <Entry.h>
#include <Message.h>
#include <Path.h>
#include <Roster.h>


extern "C" void
process_refs(entry_ref /*dir*/, BMessage* message, void* /*reserved*/)
{
	if (message == NULL)
		return;

	BMessage files;
	bool haveRef = false;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		files.AddRef("refs", &ref);
		haveRef = true;
	}
	if (!haveRef)
		return;

	ObexSendChooser::Send(files);
}
