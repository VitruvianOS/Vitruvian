/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _XVITRUVIAN_INPUT_FILTER_H
#define _XVITRUVIAN_INPUT_FILTER_H


#include <InputServerFilter.h>


extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();


// Forwards the seat stream to the per-session XVitruvian X server over a
// port the server owns. The filter never consumes: X and native apps must
// see the same events. Raw deltas and evdev codes ride as the extra
// be:* fields the device add-ons attach.
class XVitruvianInputFilter : public BInputServerFilter {
public:
								XVitruvianInputFilter();

	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	status_t			InitCheck();

private:
			port_id				_InputPort();
			void				_WriteFlat(port_id port,
									BMessage* message);
			// Announces a device once, on the first event carrying its
			// be:device_id; the DDX uses this plus the roster to keep
			// its XI2 device list current.
			void				_NoteDevices(BMessage* message);

			port_id				fPort;

	static	const int32			kMaxSeenDevices = 32;
			struct SeenDevice {
				int32	id;
				int32	subtype;
				int32	flags;
				char	name[128];
			};
			SeenDevice			fSeen[kMaxSeenDevices];
			int32				fSeenCount;
};


#endif	// _XVITRUVIAN_INPUT_FILTER_H
