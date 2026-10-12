/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexDump.h"

#include <stdio.h>

#include <ObjectList.h>

#include <MediaFile.h>

#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>


static void
_AppendDeviceDump(BMediaGraph* graph, BString& text)
{
	BObjectList<media_client_id, true> clients;
	if (graph->GetClients(&clients) != B_OK) {
		text << "devices: query failed\n";
		return;
	}

	text << "devices (" << clients.CountItems() << "):\n";
	for (int32 i = 0; i < clients.CountItems(); i++) {
		media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (graph->GetClientInfo(id, &info) != B_OK)
			continue;

		BString name;
		BString nodeName;
		BString mediaClass;
		bool isSink = false;
		bool isSource = false;
		info.FindString("name", &name);
		info.FindString("node_name", &nodeName);
		info.FindString("media.class", &mediaClass);
		info.FindBool("is.sink", &isSink);
		info.FindBool("is.source", &isSource);

		text << "  id " << id << " \"" << name << "\""
			<< " node_name=\"" << nodeName << "\""
			<< " class=" << mediaClass
			<< (isSink ? " sink" : "")
			<< (isSource ? " source" : "") << "\n";

		float master = 0.0f;
		bool mute = false;
		if (graph->GetDeviceVolume(id, &master, &mute) == B_OK)
			text << "    volume " << master << (mute ? " (muted)" : "")
				<< "\n";

		media_client_id defaultId = 0;
		if (graph->GetDefaultAudioOutput(&defaultId) == B_OK
			&& defaultId == id)
			text << "    role: default audio output\n";
		if (graph->GetDefaultAudioInput(&defaultId) == B_OK
			&& defaultId == id)
			text << "    role: default audio input\n";

		BObjectList<BMediaGraph::DeviceProfileInfo, true> profiles;
		int32 activeProfile = -1;
		if (graph->GetDeviceProfiles(id, &profiles, &activeProfile) == B_OK) {
			text << "    profiles (active " << activeProfile << "):\n";
			for (int32 p = 0; p < profiles.CountItems(); p++) {
				BMediaGraph::DeviceProfileInfo* profile = profiles.ItemAt(p);
				text << "      [" << profile->index << "] \""
					<< profile->name << "\" "
					<< (profile->available ? "available" : "unavailable")
					<< "\n";
			}
		}

		BObjectList<BMediaGraph::DevicePortInfo, true> ports;
		if (graph->GetDevicePorts(id, &ports) == B_OK) {
			text << "    ports:\n";
			for (int32 p = 0; p < ports.CountItems(); p++) {
				BMediaGraph::DevicePortInfo* port = ports.ItemAt(p);
				text << "      [" << port->portId << "] \""
					<< port->name << "\" "
					<< (port->active ? "active" : "inactive") << "\n";
			}
		}
	}
}


static void
_AppendStreamsDump(BMediaGraph* graph, BString& text)
{
	BObjectList<BMediaGraph::StreamInfo, true> streams;
	if (graph->GetStreams(&streams) != B_OK) {
		text << "streams: query failed\n";
		return;
	}

	text << "streams (" << streams.CountItems() << "):\n";
	for (int32 i = 0; i < streams.CountItems(); i++) {
		BMediaGraph::StreamInfo* stream = streams.ItemAt(i);
		text << "  id " << stream->id << " \"" << stream->name << "\""
			<< " class=" << stream->mediaClass
			<< (stream->isOutput ? " output" : " input")
			<< " device=" << stream->deviceId
			<< " volume " << stream->volume
			<< (stream->mute ? " (muted)" : "") << "\n";
	}
}


static void
_AppendForeignDump(BMediaGraph* graph, BString& text)
{
	BObjectList<BMediaGraph::ForeignNode, true> nodes;
	if (graph->GetForeignNodes(&nodes) != B_OK) {
		text << "foreign nodes: query failed\n";
		return;
	}

	text << "foreign nodes (" << nodes.CountItems() << "):\n";
	for (int32 i = 0; i < nodes.CountItems(); i++) {
		BMediaGraph::ForeignNode* node = nodes.ItemAt(i);
		text << "  id " << node->id << " \"" << node->name << "\""
			<< " class=" << node->mediaClass
			<< " ports=" << node->portCount
			<< " (out " << node->outputPorts
			<< ", in " << node->inputPorts << ")\n";

		BObjectList<BMediaGraph::ForeignPort, true> ports;
		if (graph->GetForeignPorts(node->id, &ports) != B_OK)
			continue;
		for (int32 p = 0; p < ports.CountItems(); p++) {
			BMediaGraph::ForeignPort* port = ports.ItemAt(p);
			text << "    port " << port->id << " \"" << port->name
				<< "\" " << port->direction;
			if (port->format.Length() > 0)
				text << " format=" << port->format;
			text << "\n";
		}
	}
}


static void
_AppendLinksDump(BMediaGraph* graph, BString& text)
{
	BObjectList<BMediaGraph::StreamInfo, true> streams;
	if (graph->GetStreams(&streams) != B_OK)
		return;

	int32 count = 0;
	for (int32 i = 0; i < streams.CountItems(); i++) {
		if (streams.ItemAt(i)->deviceId != 0)
			count++;
	}

	text << "links: " << count << " routing edge(s) visible\n";
	for (int32 i = 0; i < streams.CountItems(); i++) {
		BMediaGraph::StreamInfo* stream = streams.ItemAt(i);
		if (stream->deviceId == 0)
			continue;
		text << "  stream " << stream->id << " -> device "
			<< stream->deviceId << " (active, "
			<< (stream->mediaClass.FindFirst("Video") >= 0 ? "video"
				: stream->mediaClass.FindFirst("Midi") >= 0 ? "midi"
				: "audio") << ")\n";
	}
	text << "  note: BMediaGraph exposes no link enumeration query;\n"
		"  only stream-to-device routing edges are visible, own link\n"
		"  state is readable from in-process BMediaConnection only.\n";
}


status_t
RunGraphDump(BString& output)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	output << "Cortex graph dump\n";
	output << "graph instance: up\n";

	media_client_id id = 0;
	output << "defaults:\n";
	if (graph->GetDefaultAudioOutput(&id) == B_OK) {
		BMessage info;
		BString name;
		if (graph->GetClientInfo(id, &info) == B_OK)
			info.FindString("name", &name);
		output << "  audio output: id " << id << " \"" << name << "\"\n";
	} else
		output << "  audio output: none\n";

	if (graph->GetDefaultAudioInput(&id) == B_OK) {
		BMessage info;
		BString name;
		if (graph->GetClientInfo(id, &info) == B_OK)
			info.FindString("name", &name);
		output << "  audio input: id " << id << " \"" << name << "\"\n";
	} else
		output << "  audio input: none\n";

	_AppendDeviceDump(graph, output);
	_AppendStreamsDump(graph, output);
	_AppendForeignDump(graph, output);
	_AppendLinksDump(graph, output);

	output << "own clients: none in this process (headless dump)\n";
	output << "negotiated formats: readable only for in-process "
		"BMediaConnection objects\n";
	output << "driver node: not queryable through BMediaGraph\n";

	return B_OK;
}
