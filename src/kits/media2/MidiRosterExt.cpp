/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MidiRosterExt.h>

#include <new>
#include <string.h>

#include <Autolock.h>
#include <Message.h>
#include <ObjectList.h>
#include <String.h>

#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>


static BMidiRosterExt* sRosterInstance = NULL;


BMidiRosterExt::BMidiRosterExt()
	:
	fLock("midi roster ext"),
	fWatchers(10),
	fLocals(10)
{
}


BMidiRosterExt::~BMidiRosterExt()
{
	BAutolock _(fLock);
	fLocals.MakeEmpty();
	fWatchers.MakeEmpty();
}


BMidiRosterExt*
BMidiRosterExt::Instance()
{
	static BLocker sOnceLock("midi roster ext once");
	BAutolock _(sOnceLock);
	if (sRosterInstance == NULL)
		sRosterInstance = new(std::nothrow) BMidiRosterExt();
	return sRosterInstance;
}


static uint32
_directionFromClass(const BString& mediaClass, uint32 portDirection)
{
	if (mediaClass == "Midi/Source")
		return B_MIDI_ENDPOINT_SOURCE;
	if (mediaClass == "Midi/Sink")
		return B_MIDI_ENDPOINT_SINK;
	if (portDirection == B_MIDI_ENDPOINT_SOURCE
			|| portDirection == B_MIDI_ENDPOINT_SINK)
		return portDirection;
	return B_MIDI_ENDPOINT_BOTH;
}


static uint32
_portDirection(const BMediaGraph::ForeignPort& port)
{
	if (port.direction == "out")
		return B_MIDI_ENDPOINT_SOURCE;
	if (port.direction == "in")
		return B_MIDI_ENDPOINT_SINK;
	return B_MIDI_ENDPOINT_UNKNOWN;
}


static void
_makeEndpointName(BMidiEndpointInfo* info, const BMediaGraph::MidiNode& node,
	const BMediaGraph::ForeignPort* port)
{
	info->name = node.description;
	if (info->name.Length() == 0)
		info->name = node.name;
	if (info->name.Length() == 0)
		info->name << info->nodeId;

	if (port != NULL && info->portCount > 1) {
		// Multi-port devices need the port in the name or settings files
		// cannot address a single port.
		info->name << " " << port->name;
	}
}


status_t
BMidiRosterExt::GetEndpoints(
	BObjectList<BMidiEndpointInfo, true>* outEndpoints)
{
	if (outEndpoints == NULL)
		return B_BAD_VALUE;
	outEndpoints->MakeEmpty();

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	BObjectList<BMediaGraph::MidiNode, true> nodes;
	status_t err = graph->GetMidiNodes(&nodes);
	if (err != B_OK)
		return err;

	for (int32 i = 0; i < nodes.CountItems(); i++) {
		const BMediaGraph::MidiNode* node = nodes.ItemAt(i);
		if (node == NULL)
			continue;

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		err = graph->GetMidiPorts(node->id, &ports);
		if (err != B_OK)
			ports.MakeEmpty();

		BMidiEndpointInfo* info = new(std::nothrow) BMidiEndpointInfo();
		if (info == NULL)
			continue;

		info->nodeId = node->id;
		info->portId = -1;
		info->nodeName = node->name;
		info->description = node->description;
		info->mediaClass = node->mediaClass;
		info->portCount = ports.CountItems();
		info->direction = _directionFromClass(node->mediaClass,
			B_MIDI_ENDPOINT_UNKNOWN);
		info->isLocal = false;
		_makeEndpointName(info, *node, NULL);

		if (info->portCount == 0) {
			if (!outEndpoints->AddItem(info))
				delete info;
			continue;
		}

		for (int32 p = 0; p < ports.CountItems(); p++) {
			const BMediaGraph::ForeignPort* port = ports.ItemAt(p);
			if (port == NULL)
				continue;

			BMidiEndpointInfo* portInfo = new(std::nothrow)
				BMidiEndpointInfo(*info);
			if (portInfo == NULL)
				continue;

			portInfo->portId = p;
			portInfo->direction = _directionFromClass(node->mediaClass,
				_portDirection(*port));
			_makeEndpointName(portInfo, *node, port);

			if (!outEndpoints->AddItem(portInfo))
				delete portInfo;
		}

		delete info;
	}

	{
		BAutolock _(fLock);
		for (int32 i = 0; i < fLocals.CountItems(); i++) {
			LocalEntry* entry = fLocals.ItemAt(i);
			if (entry == NULL)
				continue;

			bool duplicate = false;
			for (int32 e = 0; e < outEndpoints->CountItems(); e++) {
				BMidiEndpointInfo* existing = outEndpoints->ItemAt(e);
				if (existing != NULL && existing->name == entry->name) {
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			BMidiEndpointInfo* info = new(std::nothrow) BMidiEndpointInfo();
			if (info == NULL)
				continue;

			info->nodeId = 0;
			info->portId = entry->portId;
			info->name = entry->name;
			info->nodeName = entry->name;
			info->description = entry->name;
			info->mediaClass = entry->output != NULL
				? "Local/Midi/Output" : "Local/Midi/Input";
			info->direction = entry->output != NULL
				? B_MIDI_ENDPOINT_SOURCE : B_MIDI_ENDPOINT_SINK;
			info->isLocal = true;
			info->portCount = 1;

			if (!outEndpoints->AddItem(info))
				delete info;
		}
	}

	return B_OK;
}


status_t
BMidiRosterExt::_ResolveName(const char* name, int32 portId,
	BMidiEndpointInfo* outEndpoint, uint32 wantDirection)
{
	if (name == NULL || outEndpoint == NULL)
		return B_BAD_VALUE;

	BObjectList<BMidiEndpointInfo, true> endpoints;
	status_t err = GetEndpoints(&endpoints);
	if (err != B_OK)
		return err;

	const BMidiEndpointInfo* best = NULL;
	int32 bestScore = -1;

	for (int32 i = 0; i < endpoints.CountItems(); i++) {
		const BMidiEndpointInfo* info = endpoints.ItemAt(i);
		if (info == NULL)
			continue;

		if (wantDirection != B_MIDI_ENDPOINT_UNKNOWN
				&& info->direction != wantDirection
				&& info->direction != B_MIDI_ENDPOINT_BOTH)
			continue;

		int32 score = -1;
		if (info->name == name)
			score = 3;
		else if (info->nodeName == name)
			score = 2;
		else if (info->description == name)
			score = 1;

		if (score < 0)
			continue;

		if (portId >= 0) {
			if (info->portId == portId)
				score += 4;
			else if (info->portCount > 1)
				continue;
			else if (info->portId != -1 && info->portId != portId)
				continue;
		} else if (info->portId >= 0 && info->portCount > 1) {
			// Whole-node lookup on a multi-port device: prefer port 0.
			if (info->portId != 0)
				continue;
			score += 1;
		}

		if (score > bestScore) {
			bestScore = score;
			best = info;
		}
	}

	if (best == NULL)
		return B_ENTRY_NOT_FOUND;

	*outEndpoint = *best;
	return B_OK;
}


status_t
BMidiRosterExt::FindEndpoint(const char* name,
	BMidiEndpointInfo* outEndpoint, int32 portId)
{
	return _ResolveName(name, portId, outEndpoint, B_MIDI_ENDPOINT_UNKNOWN);
}


status_t
BMidiRosterExt::FindEndpoint(uint32 nodeId, int32 portId,
	BMidiEndpointInfo* outEndpoint)
{
	if (outEndpoint == NULL)
		return B_BAD_VALUE;

	BObjectList<BMidiEndpointInfo, true> endpoints;
	status_t err = GetEndpoints(&endpoints);
	if (err != B_OK)
		return err;

	for (int32 i = 0; i < endpoints.CountItems(); i++) {
		const BMidiEndpointInfo* info = endpoints.ItemAt(i);
		if (info == NULL || info->nodeId != nodeId)
			continue;

		if (portId >= 0) {
			if (info->portId == portId) {
				*outEndpoint = *info;
				return B_OK;
			}
			continue;
		}

		if (info->portId < 0 || info->portId == 0) {
			*outEndpoint = *info;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
BMidiRosterExt::GetNodePorts(uint32 nodeId,
	BObjectList<BMidiEndpointInfo, true>* outEndpoints)
{
	if (outEndpoints == NULL)
		return B_BAD_VALUE;

	BObjectList<BMidiEndpointInfo, true> endpoints;
	status_t err = GetEndpoints(&endpoints);
	if (err != B_OK)
		return err;

	outEndpoints->MakeEmpty();
	for (int32 i = 0; i < endpoints.CountItems(); i++) {
		const BMidiEndpointInfo* info = endpoints.ItemAt(i);
		if (info == NULL || info->nodeId != nodeId)
			continue;

		BMidiEndpointInfo* copy = new(std::nothrow) BMidiEndpointInfo(*info);
		if (copy == NULL)
			continue;
		if (!outEndpoints->AddItem(copy))
			delete copy;
	}

	return B_OK;
}


status_t
BMidiRosterExt::RegisterLocal(const char* name, BMidiInput* input)
{
	if (name == NULL || input == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	for (int32 i = 0; i < fLocals.CountItems(); i++) {
		LocalEntry* entry = fLocals.ItemAt(i);
		if (entry != NULL && entry->name == name && entry->input == input)
			return B_OK;
	}

	LocalEntry* entry = new(std::nothrow) LocalEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	entry->name = name;
	entry->portId = -1;
	entry->input = input;
	entry->output = NULL;

	if (!fLocals.AddItem(entry)) {
		delete entry;
		return B_ERROR;
	}

	return B_OK;
}


status_t
BMidiRosterExt::RegisterLocal(const char* name, BMidiOutput* output)
{
	if (name == NULL || output == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	for (int32 i = 0; i < fLocals.CountItems(); i++) {
		LocalEntry* entry = fLocals.ItemAt(i);
		if (entry != NULL && entry->name == name && entry->output == output)
			return B_OK;
	}

	LocalEntry* entry = new(std::nothrow) LocalEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	entry->name = name;
	entry->portId = -1;
	entry->input = NULL;
	entry->output = output;

	if (!fLocals.AddItem(entry)) {
		delete entry;
		return B_ERROR;
	}

	return B_OK;
}


status_t
BMidiRosterExt::UnregisterLocal(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	for (int32 i = 0; i < fLocals.CountItems(); i++) {
		LocalEntry* entry = fLocals.ItemAt(i);
		if (entry != NULL && entry->name == name) {
			fLocals.RemoveItem(entry);
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
BMidiRosterExt::_FindLocalInput(const char* name, int32 /*portId*/,
	BMidiInput** outInput) const
{
	if (name == NULL || outInput == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	for (int32 i = 0; i < fLocals.CountItems(); i++) {
		LocalEntry* entry = fLocals.ItemAt(i);
		if (entry != NULL && entry->input != NULL && entry->name == name) {
			*outInput = entry->input;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
BMidiRosterExt::_FindLocalOutput(const char* name, int32 /*portId*/,
	BMidiOutput** outOutput) const
{
	if (name == NULL || outOutput == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	for (int32 i = 0; i < fLocals.CountItems(); i++) {
		LocalEntry* entry = fLocals.ItemAt(i);
		if (entry != NULL && entry->output != NULL && entry->name == name) {
			*outOutput = entry->output;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


static const BMediaGraph::ForeignPort*
_pickPort(BObjectList<BMediaGraph::ForeignPort, true>& ports,
	const char* wantDirection, int32 portId)
{
	const BMediaGraph::ForeignPort* fallback = NULL;
	for (int32 i = 0; i < ports.CountItems(); i++) {
		const BMediaGraph::ForeignPort* port = ports.ItemAt(i);
		if (port == NULL || port->direction != wantDirection)
			continue;
		if (portId >= 0) {
			if (i == portId)
				return port;
			continue;
		}
		if (fallback == NULL)
			fallback = port;
	}
	return fallback;
}


status_t
BMidiRosterExt::_ConnectForeign(const BMidiEndpointInfo& source,
	const BMidiEndpointInfo& sink)
{
	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	status_t err;

	if (source.isLocal && sink.isLocal) {
		BMidiOutput* output = NULL;
		BMidiInput* input = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;
		return graph->Connect(output, input);
	}

	if (source.isLocal && !sink.isLocal) {
		BMidiOutput* output = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		err = graph->GetMidiPorts(sink.nodeId, &ports);
		if (err != B_OK)
			return err;

		const BMediaGraph::ForeignPort* port = _pickPort(ports, "in",
			sink.portId);
		if (port == NULL)
			return B_ENTRY_NOT_FOUND;

		return graph->Connect(output, port);
	}

	if (!source.isLocal && sink.isLocal) {
		BMidiInput* input = NULL;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		err = graph->GetMidiPorts(source.nodeId, &ports);
		if (err != B_OK)
			return err;

		const BMediaGraph::ForeignPort* port = _pickPort(ports, "out",
			source.portId);
		if (port == NULL)
			return B_ENTRY_NOT_FOUND;

		return graph->Connect(port, input);
	}

	// Foreign-to-foreign: BMediaGraph has no overload, and link policy
	// between foreign nodes is not this API's job.
	return B_NOT_SUPPORTED;
}


status_t
BMidiRosterExt::Connect(const char* sourceName, int32 sourcePort,
	const char* sinkName, int32 sinkPort)
{
	BMidiEndpointInfo source;
	BMidiEndpointInfo sink;

	status_t err = _ResolveName(sourceName, sourcePort, &source,
		B_MIDI_ENDPOINT_SOURCE);
	if (err != B_OK) {
		// Local outputs are registered under their app-chosen name and do
		// not appear as graph SOURCE nodes until started.
		err = _ResolveName(sourceName, sourcePort, &source,
			B_MIDI_ENDPOINT_UNKNOWN);
		if (err != B_OK)
			return err;
	}

	err = _ResolveName(sinkName, sinkPort, &sink, B_MIDI_ENDPOINT_SINK);
	if (err != B_OK) {
		err = _ResolveName(sinkName, sinkPort, &sink,
			B_MIDI_ENDPOINT_UNKNOWN);
		if (err != B_OK)
			return err;
	}

	return _ConnectForeign(source, sink);
}


status_t
BMidiRosterExt::Connect(const char* sourceName, int32 sourcePort,
	BMidiInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;

	BMidiEndpointInfo source;
	status_t err = _ResolveName(sourceName, sourcePort, &source,
		B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (source.isLocal) {
		BMidiOutput* output = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;
		return graph->Connect(output, input);
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	err = graph->GetMidiPorts(source.nodeId, &ports);
	if (err != B_OK)
		return err;

	const BMediaGraph::ForeignPort* port = _pickPort(ports, "out",
		source.portId);
	if (port == NULL)
		return B_ENTRY_NOT_FOUND;

	return graph->Connect(port, input);
}


status_t
BMidiRosterExt::Connect(BMidiOutput* output, const char* sinkName,
	int32 sinkPort)
{
	if (output == NULL)
		return B_BAD_VALUE;

	BMidiEndpointInfo sink;
	status_t err = _ResolveName(sinkName, sinkPort, &sink,
		B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (sink.isLocal) {
		BMidiInput* input = NULL;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;
		return graph->Connect(output, input);
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	err = graph->GetMidiPorts(sink.nodeId, &ports);
	if (err != B_OK)
		return err;

	const BMediaGraph::ForeignPort* port = _pickPort(ports, "in",
		sink.portId);
	if (port == NULL)
		return B_ENTRY_NOT_FOUND;

	return graph->Connect(output, port);
}


status_t
BMidiRosterExt::Connect(BMidiOutput* output, BMidiInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	return graph->Connect(output, input);
}


status_t
BMidiRosterExt::Disconnect(const char* sourceName, int32 sourcePort,
	const char* sinkName, int32 sinkPort)
{
	BMidiEndpointInfo source;
	BMidiEndpointInfo sink;

	status_t err = _ResolveName(sourceName, sourcePort, &source,
		B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	err = _ResolveName(sinkName, sinkPort, &sink, B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (source.isLocal && sink.isLocal) {
		BMidiOutput* output = NULL;
		BMidiInput* input = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;
		return graph->Disconnect(output, input);
	}

	if (source.isLocal && !sink.isLocal) {
		BMidiOutput* output = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		err = graph->GetMidiPorts(sink.nodeId, &ports);
		if (err != B_OK)
			return err;

		const BMediaGraph::ForeignPort* port = _pickPort(ports, "in",
			sink.portId);
		if (port == NULL)
			return B_ENTRY_NOT_FOUND;

		return graph->Disconnect(output, port);
	}

	if (!source.isLocal && sink.isLocal) {
		BMidiInput* input = NULL;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		err = graph->GetMidiPorts(source.nodeId, &ports);
		if (err != B_OK)
			return err;

		const BMediaGraph::ForeignPort* port = _pickPort(ports, "out",
			source.portId);
		if (port == NULL)
			return B_ENTRY_NOT_FOUND;

		return graph->Disconnect(port, input);
	}

	return B_NOT_SUPPORTED;
}


status_t
BMidiRosterExt::Disconnect(const char* sourceName, int32 sourcePort,
	BMidiInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;

	BMidiEndpointInfo source;
	status_t err = _ResolveName(sourceName, sourcePort, &source,
		B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (source.isLocal) {
		BMidiOutput* output = NULL;
		err = _FindLocalOutput(source.name.String(), source.portId, &output);
		if (err != B_OK)
			return err;
		return graph->Disconnect(output, input);
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	err = graph->GetMidiPorts(source.nodeId, &ports);
	if (err != B_OK)
		return err;

	const BMediaGraph::ForeignPort* port = _pickPort(ports, "out",
		source.portId);
	if (port == NULL)
		return B_ENTRY_NOT_FOUND;

	return graph->Disconnect(port, input);
}


status_t
BMidiRosterExt::Disconnect(BMidiOutput* output, const char* sinkName,
	int32 sinkPort)
{
	if (output == NULL)
		return B_BAD_VALUE;

	BMidiEndpointInfo sink;
	status_t err = _ResolveName(sinkName, sinkPort, &sink,
		B_MIDI_ENDPOINT_UNKNOWN);
	if (err != B_OK)
		return err;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	if (sink.isLocal) {
		BMidiInput* input = NULL;
		err = _FindLocalInput(sink.name.String(), sink.portId, &input);
		if (err != B_OK)
			return err;
		return graph->Disconnect(output, input);
	}

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	err = graph->GetMidiPorts(sink.nodeId, &ports);
	if (err != B_OK)
		return err;

	const BMediaGraph::ForeignPort* port = _pickPort(ports, "in",
		sink.portId);
	if (port == NULL)
		return B_ENTRY_NOT_FOUND;

	return graph->Disconnect(output, port);
}


status_t
BMidiRosterExt::Disconnect(BMidiOutput* output, BMidiInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	return graph->Disconnect(output, input);
}


status_t
BMidiRosterExt::StartWatching(const BMessenger& target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	BMediaGraph* graph = MediaGraph();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	status_t err = graph->StartWatching(target, 0);
	if (err != B_OK)
		return err;

	BAutolock _(fLock);
	fWatchers.AddItem(new(std::nothrow) BMessenger(target));
	return B_OK;
}


status_t
BMidiRosterExt::StopWatching(const BMessenger& target)
{
	BAutolock _(fLock);
	for (int32 i = 0; i < fWatchers.CountItems(); i++) {
		BMessenger* messenger = fWatchers.ItemAt(i);
		if (messenger != NULL && *messenger == target) {
			fWatchers.RemoveItem(messenger);
			break;
		}
	}

	BMediaGraph* graph = MediaGraph();
	if (graph != NULL)
		graph->StopWatching(target);
	return B_OK;
}


void
BMidiRosterExt::_BroadcastRoster(uint32 /*what*/,
	const BMidiEndpointInfo* /*info*/) const
{
	// Reserved for roster-local endpoint add/remove fan-out once local
	// registration starts posting graph-visible nodes. Graph MIDI node
	// changes already reach watchers through BMediaGraph.
}
