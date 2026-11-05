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

			port_id				fPort;
};


#endif	// _XVITRUVIAN_INPUT_FILTER_H
