/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MIDI_ROSTER_EXT_H
#define _MEDIA2_MIDI_ROSTER_EXT_H


#include <Locker.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#include <media2/MidiEndpointExt.h>


class BMediaInput;
class BMediaOutput;
class BMidiInput;
class BMidiOutput;


// Extension roster over media2. Sits beside the legacy midi2 kit: it does not
// replace BMidiRoster, and it does not change those headers. Enumeration and
// connect-by-name run over BMediaGraph; local BMidiInput/BMidiOutput objects
// are visible only after RegisterLocal().
//
// App thread only. Every method that can block on a graph round-trip must not
// be called from a ProcessCallback/ProcessCycle (handoff 12.L L6).
class BMidiRosterExt {
public:
	static	BMidiRosterExt*	Instance();

	// Graph enumeration. outEndpoints receives one entry per port; a node
	// with no ports listed still appears once with portId -1.
			status_t		GetEndpoints(
									BObjectList<BMidiEndpointInfo, true>*
										outEndpoints);
			status_t		FindEndpoint(const char* name,
									BMidiEndpointInfo* outEndpoint,
									int32 portId = -1);
			status_t		FindEndpoint(uint32 nodeId, int32 portId,
									BMidiEndpointInfo* outEndpoint);
			status_t		GetNodePorts(uint32 nodeId,
									BObjectList<BMidiEndpointInfo, true>*
										outEndpoints);

	// Local connection registry for connect-by-name. The objects must be
	// started before Connect() can resolve their node/port ids.
			status_t		RegisterLocal(const char* name,
									BMidiInput* input);
			status_t		RegisterLocal(const char* name,
									BMidiOutput* output);
			status_t		UnregisterLocal(const char* name);

	// Connect-by-name. sourceName is a SOURCE (or a local BMidiOutput
	// registered under that name); sinkName is a SINK (or a local
	// BMidiInput). Foreign-to-foreign is not offered: BMediaGraph has no
	// such overload, and link policy between foreign nodes is not ours.
			status_t		Connect(const char* sourceName, int32 sourcePort,
									const char* sinkName, int32 sinkPort);
			status_t		Connect(const char* sourceName, int32 sourcePort,
									BMidiInput* input);
			status_t		Connect(BMidiOutput* output,
									const char* sinkName, int32 sinkPort);
			status_t		Connect(BMidiOutput* output, BMidiInput* input);

			status_t		Disconnect(const char* sourceName,
									int32 sourcePort, const char* sinkName,
									int32 sinkPort);
			status_t		Disconnect(const char* sourceName,
									int32 sourcePort, BMidiInput* input);
			status_t		Disconnect(BMidiOutput* output,
									const char* sinkName, int32 sinkPort);
			status_t		Disconnect(BMidiOutput* output,
									BMidiInput* input);

	// Hot-plug. Callers receive the backend's MIDI node broadcasts through
	// BMediaGraph::StartWatching; see B_MIDI_NODES_CHANGED below. The roster
	// does not own a looper of its own.
	enum {
		// Same fourcc the media2 backend broadcasts on BMediaGraph watchers.
		// Message fields: "device_id" (uint32) when a specific node changed.
		B_MIDI_NODES_CHANGED	= 'PWMC',

		// Translated convenience messages posted to roster watchers.
		B_MIDI_ENDPOINTS_CHANGED= 'MECH',
		B_MIDI_ENDPOINT_ADDED	= 'MEAD',
		B_MIDI_ENDPOINT_REMOVED	= 'MERM'
	};
			status_t		StartWatching(const BMessenger& target);
			status_t		StopWatching(const BMessenger& target);

private:
								BMidiRosterExt();
								~BMidiRosterExt();
								BMidiRosterExt(const BMidiRosterExt&);
								BMidiRosterExt& operator=(
									const BMidiRosterExt&);

			status_t		_ResolveName(const char* name, int32 portId,
									BMidiEndpointInfo* outEndpoint,
									uint32 wantDirection);
			status_t		_FindLocalInput(const char* name, int32 portId,
									BMidiInput** outInput) const;
			status_t		_FindLocalOutput(const char* name, int32 portId,
									BMidiOutput** outOutput) const;
			status_t		_ConnectForeign(const BMidiEndpointInfo& source,
									const BMidiEndpointInfo& sink);
			void			_BroadcastRoster(uint32 what,
									const BMidiEndpointInfo* info) const;

			// Mutable: the const lookup helpers take the same lock as the
			// mutating paths; BLocker is recursive.
			mutable BLocker		fLock;
			typedef BObjectList<BMessenger, false> WatcherList;
			WatcherList		fWatchers;

			struct LocalEntry {
				BString		name;
				int32		portId;
				BMidiInput*	input;
				BMidiOutput*output;
			};
			typedef BObjectList<LocalEntry, true> LocalList;
			LocalList		fLocals;
};


inline BMidiRosterExt*	MidiRosterExt() { return BMidiRosterExt::Instance(); }


#endif
