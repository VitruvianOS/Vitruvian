/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MIDI_ENDPOINT_EXT_H
#define _MEDIA2_MIDI_ENDPOINT_EXT_H


#include <String.h>
#include <SupportDefs.h>


// Direction of an endpoint port, from the PipeWire media.class and port
// direction. SOURCE produces MIDI (a keyboard); SINK consumes it (a synth).
enum midi_endpoint_direction {
	B_MIDI_ENDPOINT_UNKNOWN	= 0,
	B_MIDI_ENDPOINT_SOURCE	= 1,
	B_MIDI_ENDPOINT_SINK	= 2,
	B_MIDI_ENDPOINT_BOTH	= 3
};


// One addressable MIDI port. portId -1 selects the node as a whole when it
// has a single logical endpoint; channel is reserved for filter addressing
// and is -1 (all channels) at the roster layer.
struct BMidiEndpointAddress {
	uint32	nodeId;
	int32	portId;
	int32	channel;

								BMidiEndpointAddress(uint32 id = 0,
									int32 port = -1, int32 chan = -1)
									:
									nodeId(id),
									portId(port),
									channel(chan)
								{
								}
};


// Snapshot of one endpoint port. name is the settings-file key: it prefers
// node.description, then node.name, then a "<node>.<port>" fallback.
struct BMidiEndpointInfo {
	uint32			nodeId;
	int32			portId;
	BString			name;
	BString			nodeName;
	BString			description;
	BString			mediaClass;
	uint32			direction;
	bool			isLocal;
	uint32			portCount;

								BMidiEndpointInfo()
									:
									nodeId(0),
									portId(-1),
									direction(B_MIDI_ENDPOINT_UNKNOWN),
									isLocal(false),
									portCount(0)
								{
								}
};


#endif
