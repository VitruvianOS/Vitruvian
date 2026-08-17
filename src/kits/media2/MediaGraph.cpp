/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaGraph.h>
#include <media2/MediaConnection.h>

#include <functional>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <math.h>
#include <string.h>
#include <utility>
#include <vector>

#include <Autolock.h>
#include <Locker.h>
#include <Message.h>
#include <ObjectList.h>
#include <String.h>

#include <pipewire/core.h>
#include <pipewire/link.h>
#include <pipewire/port.h>
#include <pipewire/proxy.h>
#include <spa/param/props.h>

#include "PeakMeter.h"
#include "PipeWireBackend.h"


using namespace BPrivate::media;


static BMediaGraph* sGraphInstance = NULL;


struct LinkInfo {
	pw_proxy*		proxy;
	BMediaOutput*	output;
	BMediaInput*	input;
	uint32			outputNodeId;
	uint32			inputNodeId;
	enum pw_link_state	state;
	spa_hook		listener;
};


static BLocker sLinksLock("media graph links");
static std::map<uint32, LinkInfo> sLinks;


static void
_link_info(void* data, const struct pw_link_info* info)
{
	LinkInfo* linkInfo = (LinkInfo*)data;

	if (info == NULL)
		return;

	{
		BAutolock _(sLinksLock);
		linkInfo->state = info->state;
	}

	if (info->state == PW_LINK_STATE_ERROR || info->state == PW_LINK_STATE_UNLINKED) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL)
			backend->_Broadcast('PWLC', pw_proxy_get_bound_id(linkInfo->proxy));
	}
}


static const struct pw_link_events sLinkEvents = {
	PW_VERSION_LINK_EVENTS,
	.info = _link_info
};


static BLocker sMetersLock("media graph meters");
static std::map<uint32, PeakMeter*> sMeters;
static std::once_flag sGraphOnce;


BMediaGraph::BMediaGraph()  {}
BMediaGraph::~BMediaGraph() {}


static bool _WouldCreateCycle(uint32 outputNodeId, uint32 inputNodeId);
static bool _CheckAndReserveEdge(uint32 outputNodeId, uint32 inputNodeId);
static void _ReleaseReservedEdge(uint32 outputNodeId, uint32 inputNodeId);


BMediaGraph*
BMediaGraph::Instance()
{
	std::call_once(sGraphOnce, [] {
		if (PipeWireBackend::GetInstance() == NULL)
			return;
		sGraphInstance = new(std::nothrow) BMediaGraph();
	});
	return sGraphInstance;
}


status_t
BMediaGraph::Connect(BMediaOutput* output, BMediaInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 outNodeId = output->_GetNodeId();
	uint32 inNodeId = input->_GetNodeId();
	if (outNodeId == 0 || inNodeId == 0)
		return B_ERROR;

	if (!_CheckAndReserveEdge(outNodeId, inNodeId))
		return B_NOT_SUPPORTED;

	pw_core* core = backend->GetCore();
	if (core == NULL) {
		_ReleaseReservedEdge(outNodeId, inNodeId);
		return B_ERROR;
	}

	uint32 outPortBuf[64], inPortBuf[64];
	uint32 outCount = output->_GetPortIds(outPortBuf, 64);
	uint32 inCount = input->_GetPortIds(inPortBuf, 64);
	if (outCount == 0 || inCount == 0) {
		_ReleaseReservedEdge(outNodeId, inNodeId);
		return B_ERROR;
	}

	std::vector<std::pair<uint32, uint32> > pairs;
	if (outCount == inCount && outCount <= 64) {
		for (uint32 i = 0; i < outCount; i++)
			pairs.push_back(std::make_pair(outPortBuf[i], inPortBuf[i]));
	} else {
		pairs.push_back(std::make_pair((uint32)0, (uint32)0));
	}

	backend->Lock();

	std::vector<uint32> createdKeys;
	status_t result = B_OK;

	for (size_t i = 0; i < pairs.size(); i++) {
		pw_properties* props = pw_properties_new(NULL, NULL);
		if (props == NULL) {
			result = B_NO_MEMORY;
			break;
		}

		pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", outNodeId);
		if (pairs[i].first != 0)
			pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", pairs[i].first);
		pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", inNodeId);
		if (pairs[i].second != 0)
			pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", pairs[i].second);

		pw_proxy* proxy = (pw_proxy*)pw_core_create_object(core, "link-factory",
			PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
		pw_properties_free(props);

		if (proxy == NULL) {
			result = B_ERROR;
			break;
		}

		uint32 key = pw_proxy_get_bound_id(proxy);

		BAutolock linksLock(sLinksLock);
		LinkInfo& info = sLinks[key];
		info.proxy = proxy;
		info.output = output;
		info.input = input;
		info.outputNodeId = outNodeId;
		info.inputNodeId = inNodeId;
		info.state = PW_LINK_STATE_INIT;
		memset(&info.listener, 0, sizeof(info.listener));
		linksLock.Unlock();

		pw_link_add_listener((struct pw_link*)proxy, &info.listener,
			&sLinkEvents, &info);

		createdKeys.push_back(key);
	}

	if (result == B_OK)
		result = backend->RoundtripLocked();

	if (result == B_OK) {
		BAutolock linksLock(sLinksLock);
		for (size_t j = 0; j < createdKeys.size(); j++) {
			auto it = sLinks.find(createdKeys[j]);
			if (it != sLinks.end() && it->second.state == PW_LINK_STATE_ERROR) {
				result = B_ERROR;
				break;
			}
		}
	}

	if (result != B_OK) {
		BAutolock linksLock(sLinksLock);
		for (size_t j = 0; j < createdKeys.size(); j++) {
			auto it = sLinks.find(createdKeys[j]);
			if (it != sLinks.end()) {
				spa_hook_remove(&it->second.listener);
				pw_proxy_destroy(it->second.proxy);
				sLinks.erase(it);
			}
		}
	}

	backend->Unlock();

	_ReleaseReservedEdge(outNodeId, inNodeId);

	return result;
}


status_t
BMediaGraph::Disconnect(BMediaOutput* output, BMediaInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	bool any = false;
	{
		BAutolock _(sLinksLock);

		for (auto it = sLinks.begin(); it != sLinks.end(); ) {
			if (it->second.output == output && it->second.input == input) {
				spa_hook_remove(&it->second.listener);
				pw_proxy_destroy(it->second.proxy);
				it = sLinks.erase(it);
				any = true;
			} else {
				++it;
			}
		}
	}
	backend->Unlock();

	return any ? B_OK : B_ENTRY_NOT_FOUND;
}


status_t
BMediaGraph::GetClients(BObjectList<media_client_id, true>* clients)
{
	if (clients == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	auto devs = backend->Devices();
	for (auto& d : devs) {
		media_client_id* id = new(std::nothrow) media_client_id(
			(media_client_id)d.id);
		if (id != NULL)
			clients->AddItem(id);
	}
	return B_OK;
}


status_t
BMediaGraph::GetClientInfo(media_client_id id, BMessage* info)
{
	if (info == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	auto devs = backend->Devices();
	for (auto& d : devs) {
		if ((media_client_id)d.id != id)
			continue;
		info->AddInt32("id",          (int32)d.id);
		info->AddString("name",       d.name.c_str());
		info->AddString("node_name",  d.nodeName.c_str());
		info->AddString("media.class", d.mediaClass.c_str());
		info->AddBool("is.sink",      d.isSink);
		info->AddBool("is.source",    d.isSource);
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
BMediaGraph::GetDefaultAudioOutput(media_client_id* id)
{
	if (id == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	const uint32 viaMeta = backend->GetDefaultSinkId();
	if (viaMeta != 0) {
		*id = (media_client_id)viaMeta;
		return B_OK;
	}
	for (auto& d : backend->Devices()) {
		if (d.isSink) {
			*id = (media_client_id)d.id;
			return B_OK;
		}
	}
	*id = 0;
	return B_ENTRY_NOT_FOUND;
}


status_t
BMediaGraph::GetDefaultAudioInput(media_client_id* id)
{
	if (id == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	const uint32 viaMeta = backend->GetDefaultSourceId();
	if (viaMeta != 0) {
		*id = (media_client_id)viaMeta;
		return B_OK;
	}
	for (auto& d : backend->Devices()) {
		if (d.isSource) {
			*id = (media_client_id)d.id;
			return B_OK;
		}
	}
	*id = 0;
	return B_ENTRY_NOT_FOUND;
}


status_t
BMediaGraph::SetDefaultAudioOutput(media_client_id id)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	return backend->SetDefaultSink((uint32)id);
}


status_t
BMediaGraph::SetDefaultAudioInput(media_client_id id)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	return backend->SetDefaultSource((uint32)id);
}


status_t
BMediaGraph::StartWatching(const BMessenger& messenger,
	int32)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	backend->AddWatcher(messenger);
	return B_OK;
}


status_t
BMediaGraph::StopWatching(const BMessenger& messenger)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	backend->RemoveWatcher(messenger);
	return B_OK;
}


status_t
BMediaGraph::GetStreams(BObjectList<StreamInfo, true>* outStreams)
{
	if (outStreams == NULL)
		return B_BAD_VALUE;
	outStreams->MakeEmpty();
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	auto streams = backend->Streams();
	for (auto& s : streams) {
		StreamInfo* info = new(std::nothrow) StreamInfo();
		if (info == NULL)
			break;
		info->id         = (media_client_id)s.id;
		info->name       = s.name.c_str();
		info->mediaClass = s.mediaClass.c_str();
		info->deviceId   = (media_client_id)s.deviceId;
		info->isOutput   = s.isOutput;
		info->volume     = s.volume;
		info->mute       = s.mute;
		if (!outStreams->AddItem(info))
			delete info;
	}
	return B_OK;
}


status_t
BMediaGraph::MoveStream(media_client_id streamId, media_client_id deviceId)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	return backend->MoveStream((uint32)streamId, (uint32)deviceId);
}


status_t
BMediaGraph::SetStreamVolume(media_client_id streamId, float volume, bool mute)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	return backend->SetStreamVolume((uint32)streamId, volume, mute);
}


status_t
BMediaGraph::GetDeviceVolume(media_client_id id, float* outMaster, bool* outMute)
{
	if (outMaster == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	std::vector<float> vols;
	bool mute = false;
	status_t s = backend->GetDeviceVolume((uint32)id, vols, &mute);
	if (s != B_OK)
		return s;

	*outMaster = vols.empty() ? 0.0f : cbrtf(vols[0]);
	if (outMute != NULL)
		*outMute = mute;
	return B_OK;
}


status_t
BMediaGraph::SetDeviceVolume(media_client_id id, float master)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	float linear = master * master * master;
	return backend->SetDeviceVolume((uint32)id, &linear, 1);
}


status_t
BMediaGraph::SetDeviceChannelVolumes(media_client_id id,
	const float* volumes, size_t count)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	std::vector<float> linear(count);
	for (size_t i = 0; i < count; i++)
		linear[i] = volumes[i] * volumes[i] * volumes[i];
	return backend->SetDeviceVolume((uint32)id, linear.data(), count);
}


status_t
BMediaGraph::SetDeviceMute(media_client_id id, bool mute)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	return backend->SetDeviceMute((uint32)id, mute);
}


status_t
BMediaGraph::GetDevicePorts(media_client_id id,
	BObjectList<DevicePortInfo, true>* outPorts)
{
	if (outPorts == NULL)
		return B_BAD_VALUE;
	outPorts->MakeEmpty();

	BMediaGraph* graph = Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_NOT_SUPPORTED;

	std::vector<PipeWireBackend::DevicePort> ports;
	status_t s = backend->GetDevicePorts((uint32)id, ports);
	if (s != B_OK)
		return s;

	for (const auto& p : ports) {
		DevicePortInfo* info = new DevicePortInfo();
		info->id = id;
		info->name.SetTo(p.name.c_str());
		info->description.SetTo(p.description.c_str());
		info->portId = p.id;
		info->active = p.active;
		outPorts->AddItem(info);
	}
	return B_OK;
}


status_t
BMediaGraph::SetDevicePort(media_client_id id, int32 portId)
{
	BMediaGraph* graph = Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_NOT_SUPPORTED;

	return backend->SetDevicePort((uint32)id, portId);
}


status_t
BMediaGraph::GetDeviceProfiles(media_client_id id,
	BObjectList<DeviceProfileInfo, true>* outProfiles, int32* outActiveIndex)
{
	if (outProfiles == NULL)
		return B_BAD_VALUE;
	outProfiles->MakeEmpty();
	if (outActiveIndex != NULL)
		*outActiveIndex = -1;

	BMediaGraph* graph = Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_NOT_SUPPORTED;

	std::vector<PipeWireBackend::DeviceProfile> profiles;
	int32 active = -1;
	status_t s = backend->GetDeviceProfiles((uint32)id, profiles, &active);
	if (s != B_OK)
		return s;

	for (const auto& p : profiles) {
		DeviceProfileInfo* info = new DeviceProfileInfo();
		info->id = id;
		info->name.SetTo(p.name.c_str());
		info->description.SetTo(p.description.c_str());
		info->index = p.index;
		info->available = p.available;
		outProfiles->AddItem(info);
	}
	if (outActiveIndex != NULL)
		*outActiveIndex = active;

	return B_OK;
}


status_t
BMediaGraph::SetDeviceProfile(media_client_id id, int32 profileIndex)
{
	BMediaGraph* graph = Instance();
	if (graph == NULL)
		return B_DEVICE_NOT_FOUND;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_NOT_SUPPORTED;

	return backend->SetDeviceProfile((uint32)id, profileIndex);
}


status_t
BMediaGraph::SetDefaultDeviceVolume(float master)
{
	media_client_id id = 0;
	if (GetDefaultAudioOutput(&id) != B_OK || id == 0)
		return B_ENTRY_NOT_FOUND;
	return SetDeviceVolume(id, master);
}


status_t
BMediaGraph::StartMeteringDevice(media_client_id id, const BMessenger& target,
	bigtime_t interval)
{
	if (PipeWireBackend::GetInstance() == NULL)
		return B_DEVICE_NOT_FOUND;

	BAutolock _(sMetersLock);
	PeakMeter* meter;
	auto it = sMeters.find((uint32)id);
	if (it != sMeters.end()) {
		meter = it->second;
	} else {

		bool isSource = false;
		BString nodeName;
		BMessage info;
		if (GetClientInfo(id, &info) == B_OK) {
			info.FindBool("is.source", &isSource);
			info.FindString("node_name", &nodeName);
		}
		meter = new(std::nothrow) PeakMeter((uint32)id, isSource,
			nodeName.String());
		if (meter == NULL || meter->InitCheck() != B_OK) {
			delete meter;
			return B_ERROR;
		}
		sMeters.emplace((uint32)id, meter);
	}
	meter->StartPosting(target, interval);
	return B_OK;
}


status_t
BMediaGraph::StopMeteringDevice(media_client_id id)
{
	BAutolock _(sMetersLock);
	auto it = sMeters.find((uint32)id);
	if (it == sMeters.end())
		return B_ENTRY_NOT_FOUND;
	delete it->second;
	sMeters.erase(it);
	return B_OK;
}


struct ForeignLinkInfo {
	pw_proxy*		proxy;
	BMediaOutput*	output;
	BMediaInput*	input;
	uint32			foreignNodeId;
	uint32			foreignPortId;
	uint32			outputNodeId;
	uint32			inputNodeId;
	bool			outputIsForeign;
	enum pw_link_state	state;
	spa_hook		listener;
};


static BLocker sForeignLinksLock("media graph foreign links");
static std::map<uint32, ForeignLinkInfo> sForeignLinks;


static std::multiset<std::pair<uint32, uint32> > sPendingEdges;


static void
_foreign_link_info(void* data, const struct pw_link_info* info)
{
	ForeignLinkInfo* linkInfo = (ForeignLinkInfo*)data;

	if (info == NULL)
		return;

	{
		BAutolock _(sForeignLinksLock);
		linkInfo->state = info->state;
	}

	if (info->state == PW_LINK_STATE_ERROR || info->state == PW_LINK_STATE_UNLINKED) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL)
			backend->_Broadcast('PWLC', pw_proxy_get_bound_id(linkInfo->proxy));
	}
}


static const struct pw_link_events sForeignLinkEvents = {
	PW_VERSION_LINK_EVENTS,
	.info = _foreign_link_info
};


static bool
_WouldCreateCycle(uint32 outputNodeId, uint32 inputNodeId)
{
	if (outputNodeId == 0 || inputNodeId == 0)
		return false;

	if (outputNodeId == inputNodeId)
		return true;

	std::map<uint32, std::set<uint32>> graph;

	for (const auto& entry : sLinks) {
		const LinkInfo& link = entry.second;
		uint32 outNode = link.outputNodeId;
		uint32 inNode = link.inputNodeId;
		if (outNode != 0 && inNode != 0)
			graph[outNode].insert(inNode);
	}

	for (const auto& entry : sForeignLinks) {
		const ForeignLinkInfo& link = entry.second;
		uint32 outNode = link.outputIsForeign ? link.foreignNodeId :
			link.outputNodeId;
		uint32 inNode = link.outputIsForeign ? link.inputNodeId :
			link.foreignNodeId;
		if (outNode != 0 && inNode != 0)
			graph[outNode].insert(inNode);
	}

	for (const auto& edge : sPendingEdges)
		graph[edge.first].insert(edge.second);

	graph[outputNodeId].insert(inputNodeId);

	std::set<uint32> visited;
	std::set<uint32> recStack;

	std::function<bool(uint32)> dfs = [&](uint32 nodeId) -> bool {
		visited.insert(nodeId);
		recStack.insert(nodeId);

		for (uint32 neighbor : graph[nodeId]) {
			if (visited.find(neighbor) == visited.end()) {
				if (dfs(neighbor))
					return true;
			} else if (recStack.find(neighbor) != recStack.end()) {
				return true;
			}
		}

		recStack.erase(nodeId);
		return false;
	};

	for (const auto& entry : graph) {
		if (visited.find(entry.first) == visited.end()) {
			if (dfs(entry.first))
				return true;
		}
	}

	return false;
}


static bool
_CheckAndReserveEdge(uint32 outputNodeId, uint32 inputNodeId)
{
	BAutolock linksLock(sLinksLock);
	BAutolock foreignLock(sForeignLinksLock);

	if (_WouldCreateCycle(outputNodeId, inputNodeId))
		return false;

	sPendingEdges.insert(std::make_pair(outputNodeId, inputNodeId));
	return true;
}


static void
_ReleaseReservedEdge(uint32 outputNodeId, uint32 inputNodeId)
{
	BAutolock linksLock(sLinksLock);
	auto it = sPendingEdges.find(std::make_pair(outputNodeId, inputNodeId));
	if (it != sPendingEdges.end())
		sPendingEdges.erase(it);
}


status_t
BMediaGraph::GetForeignNodes(BObjectList<ForeignNode, true>* outNodes)
{
	if (outNodes == NULL)
		return B_BAD_VALUE;
	outNodes->MakeEmpty();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	auto nodes = backend->ForeignNodes();
	for (const auto& node : nodes) {
		ForeignNode* foreignNode = new(std::nothrow) ForeignNode();
		if (foreignNode == NULL)
			continue;

		foreignNode->id = node.id;
		foreignNode->name.SetTo(node.name.c_str());
		foreignNode->description.SetTo(node.description.c_str());
		foreignNode->mediaClass.SetTo(node.mediaClass.c_str());
		foreignNode->portCount = node.portCount;
		foreignNode->outputPorts = node.outputPorts;
		foreignNode->inputPorts = node.inputPorts;

		if (!outNodes->AddItem(foreignNode))
			delete foreignNode;
	}

	return B_OK;
}


status_t
BMediaGraph::GetForeignPorts(uint32 nodeId,
	BObjectList<ForeignPort, true>* outPorts)
{
	if (outPorts == NULL)
		return B_BAD_VALUE;
	outPorts->MakeEmpty();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	auto ports = backend->ForeignPorts(nodeId);
	for (const auto& port : ports) {
		ForeignPort* foreignPort = new(std::nothrow) ForeignPort();
		if (foreignPort == NULL)
			continue;

		foreignPort->id = port.id;
		foreignPort->nodeId = port.nodeId;
		foreignPort->name.SetTo(port.name.c_str());
		foreignPort->direction.SetTo(port.direction.c_str());
		foreignPort->format.SetTo(port.format.c_str());

		if (!outPorts->AddItem(foreignPort))
			delete foreignPort;
	}

	return B_OK;
}


status_t
BMediaGraph::Connect(BMediaOutput* output, const ForeignPort* foreignPort)
{
	if (output == NULL || foreignPort == NULL)
		return B_BAD_VALUE;

	if (strcmp(foreignPort->direction.String(), "in") != 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 outNodeId = output->_GetNodeId();
	uint32 outPortId = output->_GetPortId();

	if (outNodeId == 0 || outPortId == 0)
		return B_ERROR;

	if (!_CheckAndReserveEdge(outNodeId, foreignPort->nodeId))
		return B_NOT_SUPPORTED;

	pw_core* core = backend->GetCore();
	if (core == NULL) {
		_ReleaseReservedEdge(outNodeId, foreignPort->nodeId);
		return B_ERROR;
	}

	pw_properties* props = pw_properties_new(NULL, NULL);
	if (props == NULL) {
		_ReleaseReservedEdge(outNodeId, foreignPort->nodeId);
		return B_NO_MEMORY;
	}

	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", outNodeId);
	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", outPortId);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", foreignPort->nodeId);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", foreignPort->id);

	backend->Lock();

	pw_proxy* proxy = (pw_proxy*)pw_core_create_object(core, "link-factory",
		PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
	pw_properties_free(props);

	if (proxy == NULL) {
		backend->Unlock();
		_ReleaseReservedEdge(outNodeId, foreignPort->nodeId);
		return B_ERROR;
	}

	uint32 key = pw_proxy_get_bound_id(proxy);

	{
		BAutolock linksLock(sForeignLinksLock);
		ForeignLinkInfo& info = sForeignLinks[key];
		info.proxy = proxy;
		info.output = output;
		info.input = NULL;
		info.foreignNodeId = foreignPort->nodeId;
		info.foreignPortId = foreignPort->id;
		info.outputNodeId = outNodeId;
		info.inputNodeId = 0;
		info.outputIsForeign = false;
		info.state = PW_LINK_STATE_INIT;
		memset(&info.listener, 0, sizeof(info.listener));

		pw_link_add_listener((struct pw_link*)proxy, &info.listener,
			&sForeignLinkEvents, &info);
	}

	status_t result = backend->RoundtripLocked();
	if (result == B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end() && it->second.state == PW_LINK_STATE_ERROR)
			result = B_ERROR;
	}

	if (result != B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end()) {
			spa_hook_remove(&it->second.listener);
			pw_proxy_destroy(it->second.proxy);
			sForeignLinks.erase(it);
		}
	}

	backend->Unlock();
	_ReleaseReservedEdge(outNodeId, foreignPort->nodeId);

	return result;
}


status_t
BMediaGraph::Connect(const ForeignPort* foreignPort, BMediaInput* input)
{
	if (foreignPort == NULL || input == NULL)
		return B_BAD_VALUE;

	if (strcmp(foreignPort->direction.String(), "out") != 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 inNodeId = input->_GetNodeId();
	uint32 inPortId = input->_GetPortId();

	if (inNodeId == 0 || inPortId == 0)
		return B_ERROR;

	if (!_CheckAndReserveEdge(foreignPort->nodeId, inNodeId))
		return B_NOT_SUPPORTED;

	pw_core* core = backend->GetCore();
	if (core == NULL) {
		_ReleaseReservedEdge(foreignPort->nodeId, inNodeId);
		return B_ERROR;
	}

	pw_properties* props = pw_properties_new(NULL, NULL);
	if (props == NULL) {
		_ReleaseReservedEdge(foreignPort->nodeId, inNodeId);
		return B_NO_MEMORY;
	}

	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", foreignPort->nodeId);
	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", foreignPort->id);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", inNodeId);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", inPortId);

	backend->Lock();

	pw_proxy* proxy = (pw_proxy*)pw_core_create_object(core, "link-factory",
		PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
	pw_properties_free(props);

	if (proxy == NULL) {
		backend->Unlock();
		_ReleaseReservedEdge(foreignPort->nodeId, inNodeId);
		return B_ERROR;
	}

	uint32 key = pw_proxy_get_bound_id(proxy);
	{
		BAutolock linksLock(sForeignLinksLock);
		ForeignLinkInfo& info = sForeignLinks[key];
		info.proxy = proxy;
		info.output = NULL;
		info.input = input;
		info.foreignNodeId = foreignPort->nodeId;
		info.foreignPortId = foreignPort->id;
		info.outputNodeId = 0;
		info.inputNodeId = inNodeId;
		info.outputIsForeign = true;
		info.state = PW_LINK_STATE_INIT;
		memset(&info.listener, 0, sizeof(info.listener));

		pw_link_add_listener((struct pw_link*)proxy, &info.listener,
			&sForeignLinkEvents, &info);
	}

	status_t result = backend->RoundtripLocked();
	if (result == B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end() && it->second.state == PW_LINK_STATE_ERROR)
			result = B_ERROR;
	}

	if (result != B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end()) {
			spa_hook_remove(&it->second.listener);
			pw_proxy_destroy(it->second.proxy);
			sForeignLinks.erase(it);
		}
	}

	backend->Unlock();
	_ReleaseReservedEdge(foreignPort->nodeId, inNodeId);

	return result;
}


status_t
BMediaGraph::Disconnect(BMediaOutput* output, const ForeignPort* foreignPort)
{
	if (output == NULL || foreignPort == NULL)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	status_t result = B_ENTRY_NOT_FOUND;
	{
		BAutolock _(sForeignLinksLock);
		for (auto it = sForeignLinks.begin(); it != sForeignLinks.end(); ++it) {
			if (it->second.output == output &&
				it->second.foreignNodeId == foreignPort->nodeId &&
				it->second.foreignPortId == foreignPort->id &&
				!it->second.outputIsForeign) {
				spa_hook_remove(&it->second.listener);
				pw_proxy_destroy(it->second.proxy);
				sForeignLinks.erase(it);
				result = B_OK;
				break;
			}
		}
	}
	backend->Unlock();

	return result;
}


status_t
BMediaGraph::Disconnect(const ForeignPort* foreignPort, BMediaInput* input)
{
	if (foreignPort == NULL || input == NULL)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	status_t result = B_ENTRY_NOT_FOUND;
	{
		BAutolock _(sForeignLinksLock);
		for (auto it = sForeignLinks.begin(); it != sForeignLinks.end(); ++it) {
			if (it->second.input == input &&
				it->second.foreignNodeId == foreignPort->nodeId &&
				it->second.foreignPortId == foreignPort->id &&
				it->second.outputIsForeign) {
				spa_hook_remove(&it->second.listener);
				pw_proxy_destroy(it->second.proxy);
				sForeignLinks.erase(it);
				result = B_OK;
				break;
			}
		}
	}
	backend->Unlock();

	return result;
}

