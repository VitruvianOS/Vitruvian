/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaGraph.h>

#include <media2/CameraSettings.h>
#include <media2/CameraSource.h>
#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>

#include <algorithm>
#include <ctype.h>
#include <functional>
#include <stdio.h>
#include <string>
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
// Keyed by proxy: the bound id is not known until after the roundtrip.
static std::map<pw_proxy*, LinkInfo> sLinks;


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
			backend->_Broadcast(BMediaGraph::B_MEDIA_LINKS_CHANGED,
				pw_proxy_get_bound_id(linkInfo->proxy));
	}
}


static const struct pw_link_events sLinkEvents = {
	PW_VERSION_LINK_EVENTS,
	.info = _link_info
};


static BLocker sMetersLock("media graph meters");
static std::map<uint32, PeakMeter*> sMeters;
static std::once_flag sGraphOnce;


struct InProcessLink {
	BMediaOutput*	output;
	BMediaInput*	input;
};


static BLocker sInProcessLock("media graph in-process links");
static std::vector<InProcessLink> sInProcessLinks;


BMediaGraph::BMediaGraph()  {}
BMediaGraph::~BMediaGraph() {}


static bool _WouldCreateCycle(uint32 outputNodeId, uint32 inputNodeId);
static bool _CheckAndReserveEdge(uint32 outputNodeId, uint32 inputNodeId);
static void _ReleaseReservedEdge(uint32 outputNodeId, uint32 inputNodeId);


BMediaGraph*
BMediaGraph::Instance()
{
	// Always created: in-process (GStreamer-backed) clients route through the
	// graph even when the PipeWire backend is unavailable.
	std::call_once(sGraphOnce, [] {
		sGraphInstance = new(std::nothrow) BMediaGraph();
	});
	return sGraphInstance;
}


status_t
BMediaGraph::Connect(BMediaOutput* output, BMediaInput* input)
{
	if (output == NULL || input == NULL)
		return B_BAD_VALUE;

	BMediaClient* outClient = output->Client();
	BMediaClient* inClient = input->Client();
	if (outClient != NULL && inClient != NULL
		&& outClient->IsInProcessRouted() && inClient->IsInProcessRouted()) {
		return _ConnectInProcess(output, input);
	}

	// A unit joins an in-process peer by running detached; once detached it
	// cannot take graph peers, and detached-only units never can.
	if (outClient != NULL && inClient != NULL) {
		bool detached = false;
		status_t result = _ResolveDetached(outClient, inClient, &detached);
		if (result != B_OK)
			return result;
		if (detached)
			return _ConnectInProcess(output, input);
	}

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	// A started unit gets its node id from the server asynchronously.
	uint32 outNodeId = output->_GetNodeId();
	uint32 inNodeId = input->_GetNodeId();
	for (int i = 0; (outNodeId == 0 || inNodeId == 0) && i < 100; i++) {
		snooze(10000);
		outNodeId = output->_GetNodeId();
		inNodeId = input->_GetNodeId();
	}
	if (outNodeId == 0 || inNodeId == 0)
		return B_ERROR;

	// Links-only format gate: refuse when both sides already carry a
	// concrete format and they cannot match. Real negotiation stays out.
	const BMediaFormat& outFormat = output->Format();
	const BMediaFormat& inFormat = input->Format();
	if (outFormat.Type() != B_MEDIA_NO_TYPE
			&& inFormat.Type() != B_MEDIA_NO_TYPE
			&& !outFormat.Matches(inFormat)) {
		return B_BAD_VALUE;
	}

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

	// An identical explicit link already exists: nothing to do.
	{
		BAutolock linksLock(sLinksLock);
		for (const auto& entry : sLinks) {
			if (entry.second.output == output && entry.second.input == input
					&& entry.second.state != PW_LINK_STATE_ERROR
					&& entry.second.state != PW_LINK_STATE_UNLINKED) {
				_ReleaseReservedEdge(outNodeId, inNodeId);
				return B_OK;
			}
		}
	}

	backend->Lock();

	std::vector<pw_proxy*> createdKeys;
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

		pw_proxy* key = proxy;

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

	BMediaClient* outClient = output->Client();
	BMediaClient* inClient = input->Client();
	if (outClient != NULL && inClient != NULL
		&& outClient->IsInProcessRouted() && inClient->IsInProcessRouted()) {
		return _DisconnectInProcess(output, input);
	}

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
BMediaGraph::_ResolveDetached(BMediaClient* outClient, BMediaClient* inClient,
	bool* detached)
{
	BMediaClient* clients[2] = { outClient, inClient };
	BMediaUnit* units[2] = {
		dynamic_cast<BMediaUnit*>(outClient),
		dynamic_cast<BMediaUnit*>(inClient)
	};

	for (int i = 0; i < 2; i++) {
		BMediaUnit* unit = units[i];
		if (unit == NULL)
			continue;
		BMediaClient* peer = clients[1 - i];

		if (peer->IsInProcessRouted()) {
			status_t status = unit->_EnterDetached();
			if (status != B_OK)
				return status;
			*detached = true;
		} else if (unit->IsDetachedOnly()) {
			return B_NOT_SUPPORTED;
		} else if (unit->IsDetached()) {
			return B_NOT_ALLOWED;
		}
	}
	return B_OK;
}


status_t
BMediaGraph::_ConnectInProcess(BMediaOutput* output, BMediaInput* input)
{
	if (output->IsConnected() || input->IsConnected())
		return B_NOT_ALLOWED;

	// Downstream request wins; the upstream offer is the fallback.
	BMediaFormat format = input->AcceptedFormat();
	if (format.Type() == B_MEDIA_NO_TYPE)
		format = output->Format();
	if (format.Type() == B_MEDIA_NO_TYPE)
		format = input->Format();
	if (format.Type() == B_MEDIA_NO_TYPE)
		return B_BAD_VALUE;

	status_t status = output->PrepareToConnect(&format);
	if (status != B_OK)
		return status;

	status = input->AcceptFormat(&format);
	if (status != B_OK)
		return status;

	{
		BAutolock _(sInProcessLock);
		InProcessLink link;
		link.output = output;
		link.input = input;
		sInProcessLinks.push_back(link);
	}

	output->fBinding = input;
	input->fBinding = output;
	BMediaUnit* outUnit = dynamic_cast<BMediaUnit*>(output->Client());
	BMediaUnit* inUnit = dynamic_cast<BMediaUnit*>(input->Client());
	if (outUnit != NULL && outUnit->IsDetached())
		outUnit->_DetachedLink(output, true);
	if (inUnit != NULL && inUnit->IsDetached())
		inUnit->_DetachedLink(input, true);
	output->Connected(format);
	input->Connected(format);
	return B_OK;
}


status_t
BMediaGraph::_DisconnectInProcess(BMediaOutput* output, BMediaInput* input)
{
	bool found = false;
	{
		BAutolock _(sInProcessLock);
		for (size_t i = 0; i < sInProcessLinks.size(); i++) {
			if (sInProcessLinks[i].output == output
				&& sInProcessLinks[i].input == input) {
				sInProcessLinks.erase(sInProcessLinks.begin() + i);
				found = true;
				break;
			}
		}
	}
	if (!found)
		return B_ENTRY_NOT_FOUND;

	output->fBinding = NULL;
	input->fBinding = NULL;
	BMediaUnit* outUnit = dynamic_cast<BMediaUnit*>(output->Client());
	BMediaUnit* inUnit = dynamic_cast<BMediaUnit*>(input->Client());
	if (outUnit != NULL && output->fDetachedUnit == outUnit)
		outUnit->_DetachedLink(output, false);
	if (inUnit != NULL && input->fDetachedUnit == inUnit)
		inUnit->_DetachedLink(input, false);
	output->Disconnected();
	input->Disconnected();
	return B_OK;
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
		info->AddBool("is.video",     d.isVideo);
		info->AddString("stable_id",  d.stableId.c_str());
		info->AddString("location",   d.location.c_str());
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
BMediaGraph::GetAudioInputs(BObjectList<AudioDevice, true>* outDevices)
{
	if (outDevices == NULL)
		return B_BAD_VALUE;
	outDevices->MakeEmpty();

	BObjectList<media_client_id, true> clients;
	status_t status = GetClients(&clients);
	if (status != B_OK)
		return status;

	for (int32 i = 0; i < clients.CountItems(); i++) {
		const media_client_id id = *clients.ItemAt(i);
		BMessage info;
		if (GetClientInfo(id, &info) != B_OK)
			continue;
		bool isSource = false;
		bool isVideo = false;
		info.FindBool("is.source", &isSource);
		info.FindBool("is.video", &isVideo);
		if (!isSource || isVideo)
			continue;

		AudioDevice* device = new(std::nothrow) AudioDevice();
		if (device == NULL)
			break;
		device->id = id;
		if (info.FindString("name", &device->name) != B_OK
			|| device->name.Length() == 0) {
			info.FindString("node_name", &device->name);
		}
		if (!outDevices->AddItem(device))
			delete device;
	}
	return B_OK;
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


static camera_location
_CameraLocationFromString(const BString& location)
{
	if (location == "front")
		return B_CAMERA_LOCATION_FRONT;
	if (location == "back")
		return B_CAMERA_LOCATION_BACK;
	if (location == "external" || location == "libcamera")
		return B_CAMERA_LOCATION_EXTERNAL;
	return B_CAMERA_LOCATION_UNKNOWN;
}


// Settings file wins; PipeWire metadata is the mirror for other clients.
status_t
BMediaGraph::GetDefaultCamera(media_client_id* id)
{
	if (id == NULL)
		return B_BAD_VALUE;

	BCameraSettings settings;
	BString stableId;
	if (settings.Load() == B_OK)
		stableId = settings.DefaultCameraId();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	if (!stableId.IsEmpty()) {
		for (auto& d : backend->Devices()) {
			if (d.isVideo && d.stableId == stableId.String()) {
				*id = (media_client_id)d.id;
				return B_OK;
			}
		}
	}

	const uint32 viaMeta = backend->GetDefaultVideoSourceId();
	if (viaMeta != 0) {
		*id = (media_client_id)viaMeta;
		return B_OK;
	}

	for (auto& d : backend->Devices()) {
		if (d.isVideo) {
			*id = (media_client_id)d.id;
			return B_OK;
		}
	}
	*id = 0;
	return B_ENTRY_NOT_FOUND;
}


status_t
BMediaGraph::SetDefaultCamera(media_client_id id)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	// Persist first so the settings file remains the source of truth.
	BString stableId;
	auto devs = backend->Devices();
	for (auto& d : devs) {
		if (d.isVideo && d.id == (uint32)id) {
			stableId = d.stableId.c_str();
			break;
		}
	}

	if (!stableId.IsEmpty()) {
		BCameraSettings settings;
		settings.Load();
		settings.SetDefaultCameraId(stableId);
		camera_device_info info;
		info.id = id;
		info.stableId = stableId;
		info.location = B_CAMERA_LOCATION_UNKNOWN;
		for (auto& d : devs) {
			if (d.isVideo && d.id == (uint32)id) {
				info.name = d.name.c_str();
				info.location = _CameraLocationFromString(d.location.c_str());
				break;
			}
		}
		settings.SetDevice(info);
		settings.Save();
	}

	return backend->SetDefaultVideoSource((uint32)id);
}


status_t
BMediaGraph::StartWatching(const BMessenger& messenger,
	int32 notificationMask)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;
	backend->AddWatcher(messenger, notificationMask);
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
	// Both sides foreign: the input side lives in inputForeign*.
	bool			inputIsForeign;
	uint32			inputForeignNodeId;
	uint32			inputForeignPortId;
	enum pw_link_state	state;
	spa_hook		listener;
	spa_hook		proxyListener;
};


static BLocker sForeignLinksLock("media graph foreign links");
// Keyed by proxy: the bound id is not known until after the roundtrip.
static std::map<pw_proxy*, ForeignLinkInfo> sForeignLinks;


// Whether a foreign link still holds our endpoint.
static bool
_HasForeignLink(const BMediaConnection* connection)
{
	BAutolock _(sForeignLinksLock);
	for (auto& entry : sForeignLinks) {
		if (entry.second.output == connection
				|| entry.second.input == connection)
			return true;
	}
	return false;
}


static std::multiset<std::pair<uint32, uint32> > sPendingEdges;


// Caller holds sForeignLinksLock (and the backend lock for the proxy).
static void
_release_foreign_link(ForeignLinkInfo& info)
{
	spa_hook_remove(&info.listener);
	spa_hook_remove(&info.proxyListener);
	pw_proxy_destroy(info.proxy);
}


// PipeWire deleted the link, e.g. its device went away. Loop thread, backend
// lock held.
void
PipeWireBackend::_ForeignLinkRemoved(void* data)
{
	ForeignLinkInfo* linkInfo = (ForeignLinkInfo*)data;
	BMediaConnection* endpoint = linkInfo->output != NULL
		? (BMediaConnection*)linkInfo->output
		: (BMediaConnection*)linkInfo->input;
	{
		BAutolock _(sForeignLinksLock);
		auto it = sForeignLinks.find(linkInfo->proxy);
		if (it == sForeignLinks.end())
			return;
		_release_foreign_link(it->second);
		sForeignLinks.erase(it);
	}

	if (endpoint != NULL && endpoint->IsConnected()
			&& !endpoint->HasBinding() && !_HasForeignLink(endpoint))
		endpoint->Disconnected();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend != NULL)
		backend->_Broadcast(BMediaGraph::B_MEDIA_LINKS_CHANGED);
}


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
			backend->_Broadcast(BMediaGraph::B_MEDIA_LINKS_CHANGED,
				pw_proxy_get_bound_id(linkInfo->proxy));
	}
}


static const struct pw_link_events sForeignLinkEvents = {
	PW_VERSION_LINK_EVENTS,
	.info = _foreign_link_info
};


static const struct pw_proxy_events sForeignProxyEvents = {
	PW_VERSION_PROXY_EVENTS,
	.removed = PipeWireBackend::_ForeignLinkRemoved
};


static void
_add_foreign_link_listeners(ForeignLinkInfo& info)
{
	memset(&info.listener, 0, sizeof(info.listener));
	memset(&info.proxyListener, 0, sizeof(info.proxyListener));
	pw_link_add_listener((struct pw_link*)info.proxy, &info.listener,
		&sForeignLinkEvents, &info);
	pw_proxy_add_listener(info.proxy, &info.proxyListener,
		&sForeignProxyEvents, &info);
}


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
		uint32 outNode = 0;
		uint32 inNode = 0;
		if (link.output != NULL) {
			outNode = link.outputNodeId;
			inNode = link.foreignNodeId;
		} else if (link.input != NULL) {
			outNode = link.foreignNodeId;
			inNode = link.inputNodeId;
		} else {
			outNode = link.foreignNodeId;
			inNode = link.inputForeignNodeId;
		}
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
BMediaGraph::GetLinks(BObjectList<GraphLink, true>* outLinks)
{
	if (outLinks == NULL)
		return B_BAD_VALUE;
	outLinks->MakeEmpty();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	auto links = backend->Links();
	for (const auto& link : links) {
		GraphLink* graphLink = new(std::nothrow) GraphLink();
		if (graphLink == NULL)
			continue;

		graphLink->id = link.id;
		graphLink->outputNodeId = link.outputNodeId;
		graphLink->outputPortId = link.outputPortId;
		graphLink->inputNodeId = link.inputNodeId;
		graphLink->inputPortId = link.inputPortId;

		if (!outLinks->AddItem(graphLink))
			delete graphLink;
	}

	return B_OK;
}


status_t
BMediaGraph::DestroyLink(uint32 linkId)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	return backend->DestroyLink(linkId);
}


uint32
BMediaGraph::GetConnectionNodeId(const BMediaConnection* connection)
{
	if (connection == NULL)
		return 0;

	return connection->_GetNodeId();
}


uint32
BMediaGraph::GetConnectionPortIds(const BMediaConnection* connection,
	uint32* outIds, uint32 maxCount)
{
	if (connection == NULL || outIds == NULL)
		return 0;

	return connection->_GetPortIds(outIds, maxCount);
}


status_t
BMediaGraph::GetMidiNodes(BObjectList<MidiNode, true>* outNodes)
{
	if (outNodes == NULL)
		return B_BAD_VALUE;
	outNodes->MakeEmpty();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	auto nodes = backend->MidiNodes();
	for (const auto& node : nodes) {
		MidiNode* midiNode = new(std::nothrow) MidiNode();
		if (midiNode == NULL)
			continue;

		midiNode->id = node.id;
		midiNode->name.SetTo(node.name.c_str());
		midiNode->description.SetTo(node.description.c_str());
		midiNode->mediaClass.SetTo(node.mediaClass.c_str());

		if (!outNodes->AddItem(midiNode))
			delete midiNode;
	}

	return B_OK;
}


status_t
BMediaGraph::GetMidiPorts(uint32 nodeId,
	BObjectList<ForeignPort, true>* outPorts)
{
	// Ports for MIDI nodes share the registry index with every other port.
	return GetForeignPorts(nodeId, outPorts);
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

	pw_proxy* key = proxy;

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
		info.inputIsForeign = true;
		info.inputForeignNodeId = foreignPort->nodeId;
		info.inputForeignPortId = foreignPort->id;
		info.state = PW_LINK_STATE_INIT;
		_add_foreign_link_listeners(info);
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
			_release_foreign_link(it->second);
			sForeignLinks.erase(it);
		}
	}

	backend->Unlock();
	_ReleaseReservedEdge(outNodeId, foreignPort->nodeId);
	// A foreign link connects our endpoint as a binding would.
	if (result == B_OK && !output->IsConnected())
		output->Connected(output->AcceptedFormat());

	return result;
}


status_t
BMediaGraph::Connect(const ForeignPort* foreignPort, BMediaInput* input)
{
	return Connect(foreignPort, input, 0);
}


// Channel order: FL/left first, FR/right second, anything else by name.
static int
_ChannelRank(const std::string& name)
{
	std::string n = name;
	for (size_t i = 0; i < n.size(); i++)
		n[i] = (char)tolower((unsigned char)n[i]);
	size_t us = n.find_last_of('_');
	std::string suffix = us == std::string::npos ? n : n.substr(us + 1);
	if (suffix == "fl" || suffix == "left" || suffix == "mono")
		return 0;
	if (suffix == "fr" || suffix == "right")
		return 1;
	return 2;
}


static bool
_ChannelLess(const PipeWireBackend::ForeignPortInfo& a,
	const PipeWireBackend::ForeignPortInfo& b)
{
	int ra = _ChannelRank(a.name), rb = _ChannelRank(b.name);
	if (ra != rb)
		return ra < rb;
	if (a.name != b.name)
		return a.name < b.name;
	return a.id < b.id;
}


status_t
BMediaGraph::ConnectChannels(uint32 foreignNodeId, BMediaInput* input)
{
	if (input == NULL)
		return B_BAD_VALUE;
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	// Wait for the stream to be bound to a node (2 s deadline).
	const bigtime_t deadline = system_time() + 2000000;
	uint32 inNodeId = input->_GetNodeId();
	while (inNodeId == 0 && system_time() < deadline) {
		backend->Lock();
		backend->RoundtripLocked(500000);
		backend->Unlock();
		inNodeId = input->_GetNodeId();
		if (inNodeId == 0)
			snooze(20000);
	}
	if (inNodeId == 0) {
		fprintf(stderr, "ConnectChannels: input stream has no node id\n");
		return B_TIMED_OUT;
	}

	// Blocks (bounded) until the input ports are registered.
	std::vector<uint32> inIds;
	status_t err = backend->ResolveNodePorts(inNodeId, PW_DIRECTION_INPUT,
		inIds, std::max<bigtime_t>(deadline - system_time(), 200000));
	if (err != B_OK) {
		fprintf(stderr, "ConnectChannels: node %u has no input ports (0x%x)\n",
			(unsigned)inNodeId, (unsigned)err);
		return err;
	}

	std::vector<PipeWireBackend::ForeignPortInfo> outs, ins;
	std::vector<PipeWireBackend::ForeignPortInfo> all
		= backend->ForeignPorts(foreignNodeId);
	for (size_t i = 0; i < all.size(); i++)
		if (all[i].direction == "out")
			outs.push_back(all[i]);
	all = backend->ForeignPorts(inNodeId);
	for (size_t i = 0; i < all.size(); i++)
		if (all[i].direction == "in")
			ins.push_back(all[i]);
	std::sort(outs.begin(), outs.end(), _ChannelLess);
	std::sort(ins.begin(), ins.end(), _ChannelLess);
	fprintf(stderr, "ConnectChannels: src node %u (%zu out), dst node %u"
		" (%zu in)\n", (unsigned)foreignNodeId, outs.size(),
		(unsigned)inNodeId, ins.size());
	if (outs.empty() || ins.empty())
		return B_ENTRY_NOT_FOUND;

	// Mono source feeds every input; otherwise pair by position.
	size_t count = outs.size() == 1 ? ins.size()
		: std::min(outs.size(), ins.size());
	int linked = 0;
	status_t last = B_ERROR;
	for (size_t i = 0; i < count; i++) {
		const PipeWireBackend::ForeignPortInfo& o
			= outs.size() == 1 ? outs[0] : outs[i];
		ForeignPort fp;
		fp.id = o.id;
		fp.nodeId = o.nodeId;
		fp.name = o.name.c_str();
		fp.direction = "out";
		last = Connect(&fp, input, ins[i].id);
		if (last == B_OK)
			linked++;
		else
			fprintf(stderr, "ConnectChannels: link %u -> %u failed (0x%x)\n",
				(unsigned)o.id, (unsigned)ins[i].id, (unsigned)last);
	}
	return linked > 0 ? B_OK : last;
}


status_t
BMediaGraph::Connect(const ForeignPort* foreignPort, BMediaInput* input,
	uint32 inPortId)
{
	if (foreignPort == NULL || input == NULL)
		return B_BAD_VALUE;

	if (strcmp(foreignPort->direction.String(), "out") != 0)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 inNodeId = input->_GetNodeId();
	if (inPortId == 0)
		inPortId = input->_GetPortId();

	if (inNodeId == 0 || inPortId == 0) {
		fprintf(stderr, "Connect: input not ready (node %u, port %u)\n",
			(unsigned)inNodeId, (unsigned)inPortId);
		return B_ERROR;
	}

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

	pw_proxy* key = proxy;
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
		info.inputIsForeign = false;
		info.inputForeignNodeId = 0;
		info.inputForeignPortId = 0;
		info.state = PW_LINK_STATE_INIT;
		_add_foreign_link_listeners(info);
	}

	status_t result = backend->RoundtripLocked();
	if (result == B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end()
			&& it->second.state == PW_LINK_STATE_ERROR) {
			fprintf(stderr, "Connect: link state ERROR (%u -> %u)\n",
				(unsigned)foreignPort->id, (unsigned)inPortId);
			result = B_ERROR;
		}
	}

	if (result != B_OK) {
		BAutolock linksLock(sForeignLinksLock);
		auto it = sForeignLinks.find(key);
		if (it != sForeignLinks.end()) {
			_release_foreign_link(it->second);
			sForeignLinks.erase(it);
		}
	}

	backend->Unlock();
	_ReleaseReservedEdge(foreignPort->nodeId, inNodeId);
	// A foreign link connects our endpoint as a binding would.
	if (result == B_OK && !input->IsConnected())
		input->Connected(input->AcceptedFormat());

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
				_release_foreign_link(it->second);
				sForeignLinks.erase(it);
				result = B_OK;
				break;
			}
		}
	}
	backend->Unlock();
	if (output->IsConnected() && !output->HasBinding()
			&& !_HasForeignLink(output))
		output->Disconnected();

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
		// A stereo source holds one link per channel: drop them all.
		for (auto it = sForeignLinks.begin(); it != sForeignLinks.end();) {
			if (it->second.input == input &&
				it->second.foreignNodeId == foreignPort->nodeId &&
				it->second.outputIsForeign) {
				_release_foreign_link(it->second);
				it = sForeignLinks.erase(it);
				result = B_OK;
			} else
				++it;
		}
	}
	backend->Unlock();
	if (input->IsConnected() && !input->HasBinding()
			&& !_HasForeignLink(input))
		input->Disconnected();

	return result;
}


static const char*
_link_state_name(enum pw_link_state state)
{
	switch (state) {
		case PW_LINK_STATE_INIT:        return "init";
		case PW_LINK_STATE_NEGOTIATING: return "negotiating";
		case PW_LINK_STATE_ALLOCATING:  return "allocating";
		case PW_LINK_STATE_PAUSED:      return "paused";
		case PW_LINK_STATE_ACTIVE:      return "active";
		case PW_LINK_STATE_ERROR:       return "error";
		case PW_LINK_STATE_UNLINKED:    return "unlinked";
		default:                        return "unknown";
	}
}



status_t
BMediaGraph::Connect(const ForeignPort* outputPort,
	const ForeignPort* inputPort)
{
	if (outputPort == NULL || inputPort == NULL)
		return B_BAD_VALUE;

	if (strcmp(outputPort->direction.String(), "out") != 0
		|| strcmp(inputPort->direction.String(), "in") != 0) {
		return B_BAD_VALUE;
	}

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	if (!_CheckAndReserveEdge(outputPort->nodeId, inputPort->nodeId))
		return B_NOT_SUPPORTED;

	pw_core* core = backend->GetCore();
	if (core == NULL) {
		_ReleaseReservedEdge(outputPort->nodeId, inputPort->nodeId);
		return B_ERROR;
	}

	pw_properties* props = pw_properties_new(NULL, NULL);
	if (props == NULL) {
		_ReleaseReservedEdge(outputPort->nodeId, inputPort->nodeId);
		return B_NO_MEMORY;
	}

	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u",
		outputPort->nodeId);
	pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", outputPort->id);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u",
		inputPort->nodeId);
	pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", inputPort->id);

	backend->Lock();

	pw_proxy* proxy = (pw_proxy*)pw_core_create_object(core, "link-factory",
		PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
	pw_properties_free(props);

	if (proxy == NULL) {
		backend->Unlock();
		_ReleaseReservedEdge(outputPort->nodeId, inputPort->nodeId);
		return B_ERROR;
	}

	pw_proxy* key = proxy;
	{
		BAutolock linksLock(sForeignLinksLock);
		ForeignLinkInfo& info = sForeignLinks[key];
		info.proxy = proxy;
		info.output = NULL;
		info.input = NULL;
		info.foreignNodeId = outputPort->nodeId;
		info.foreignPortId = outputPort->id;
		info.outputNodeId = 0;
		info.inputNodeId = 0;
		info.outputIsForeign = true;
		info.inputIsForeign = true;
		info.inputForeignNodeId = inputPort->nodeId;
		info.inputForeignPortId = inputPort->id;
		info.state = PW_LINK_STATE_INIT;
		_add_foreign_link_listeners(info);
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
			_release_foreign_link(it->second);
			sForeignLinks.erase(it);
		}
	}

	backend->Unlock();
	_ReleaseReservedEdge(outputPort->nodeId, inputPort->nodeId);

	return result;
}


status_t
BMediaGraph::DumpGraph(BString& out) const
{
	out << "graph dump\n";

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL) {
		out << "  backend unavailable\n";
		return B_DEVICE_NOT_FOUND;
	}

	out << "nodes:\n";
	for (const auto& d : backend->Devices()) {
		out << "  id=" << d.id << " card=" << d.cardId
			<< " name=\"" << d.name.c_str() << "\""
			<< " node=\"" << d.nodeName.c_str() << "\""
			<< " class=\"" << d.mediaClass.c_str() << "\""
			<< (d.isSink ? " sink" : "")
			<< (d.isSource ? " source" : "")
			<< "\n";
	}

	out << "streams:\n";
	for (const auto& s : backend->Streams()) {
		out << "  id=" << s.id << " name=\"" << s.name.c_str() << "\""
			<< " class=\"" << s.mediaClass.c_str() << "\""
			<< " device=" << s.deviceId
			<< (s.isOutput ? " out" : " in")
			<< " vol=" << s.volume << (s.mute ? " mute" : "")
			<< "\n";
	}

	out << "foreign nodes:\n";
	for (const auto& n : backend->ForeignNodes()) {
		out << "  id=" << n.id << " name=\"" << n.name.c_str() << "\""
			<< " class=\"" << n.mediaClass.c_str() << "\""
			<< " ports=" << n.portCount
			<< " (out=" << n.outputPorts << " in=" << n.inputPorts << ")"
			<< "\n";
		for (const auto& p : backend->ForeignPorts(n.id)) {
			out << "    port id=" << p.id << " node=" << p.nodeId
				<< " name=\"" << p.name.c_str() << "\""
				<< " dir=" << p.direction.c_str()
				<< " fmt=\"" << p.format.c_str() << "\""
				<< "\n";
		}
	}

	out << "links:\n";
	{
		BAutolock _(sLinksLock);
		for (const auto& entry : sLinks) {
			out << "  id=" << pw_proxy_get_bound_id(entry.first)
				<< " outNode=" << entry.second.outputNodeId
				<< " inNode=" << entry.second.inputNodeId
				<< " state=" << _link_state_name(entry.second.state)
				<< "\n";
		}
	}
	{
		BAutolock _(sForeignLinksLock);
		for (const auto& entry : sForeignLinks) {
			out << "  id=" << pw_proxy_get_bound_id(entry.first)
				<< " outNode=" << entry.second.outputNodeId
				<< " inNode=" << (entry.second.outputIsForeign
					? entry.second.foreignNodeId : entry.second.inputNodeId)
				<< " foreignPort=" << entry.second.foreignPortId
				<< " state=" << _link_state_name(entry.second.state)
				<< "\n";
		}
	}

	return B_OK;
}


status_t
BMediaGraph::Disconnect(const ForeignPort* outputPort,
	const ForeignPort* inputPort)
{
	if (outputPort == NULL || inputPort == NULL)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	status_t result = B_ENTRY_NOT_FOUND;
	{
		BAutolock _(sForeignLinksLock);
		for (auto it = sForeignLinks.begin(); it != sForeignLinks.end(); ++it) {
			ForeignLinkInfo& link = it->second;
			if (link.output == NULL && link.input == NULL
				&& link.inputIsForeign
				&& link.foreignNodeId == outputPort->nodeId
				&& link.foreignPortId == outputPort->id
				&& link.inputForeignNodeId == inputPort->nodeId
				&& link.inputForeignPortId == inputPort->id) {
				_release_foreign_link(link);
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
BMediaGraph::GetLinks(BObjectList<Link, true>* outLinks)
{
	if (outLinks == NULL)
		return B_BAD_VALUE;
	outLinks->MakeEmpty();

	{
		BAutolock _(sLinksLock);
		for (const auto& entry : sLinks) {
			const LinkInfo& info = entry.second;
			Link* link = new(std::nothrow) Link();
			if (link == NULL)
				continue;
			link->inProcess = false;
			link->outputNodeId = info.outputNodeId;
			link->outputPortId = info.output != NULL
				? info.output->_GetPortId() : 0;
			link->inputNodeId = info.inputNodeId;
			link->inputPortId = info.input != NULL
				? info.input->_GetPortId() : 0;
			link->output = info.output;
			link->input = info.input;
			if (!outLinks->AddItem(link))
				delete link;
		}
	}

	{
		BAutolock _(sForeignLinksLock);
		for (const auto& entry : sForeignLinks) {
			const ForeignLinkInfo& info = entry.second;
			Link* link = new(std::nothrow) Link();
			if (link == NULL)
				continue;
			link->inProcess = false;
			link->output = info.output;
			link->input = info.input;
			if (info.output != NULL) {
				link->outputNodeId = info.outputNodeId;
				link->outputPortId = info.output->_GetPortId();
				link->inputNodeId = info.foreignNodeId;
				link->inputPortId = info.foreignPortId;
			} else if (info.input != NULL) {
				link->outputNodeId = info.foreignNodeId;
				link->outputPortId = info.foreignPortId;
				link->inputNodeId = info.inputNodeId;
				link->inputPortId = info.input->_GetPortId();
			} else {
				link->outputNodeId = info.foreignNodeId;
				link->outputPortId = info.foreignPortId;
				link->inputNodeId = info.inputForeignNodeId;
				link->inputPortId = info.inputForeignPortId;
			}
			if (!outLinks->AddItem(link))
				delete link;
		}
	}

	{
		BAutolock _(sInProcessLock);
		for (const auto& info : sInProcessLinks) {
			Link* link = new(std::nothrow) Link();
			if (link == NULL)
				continue;
			link->inProcess = true;
			link->outputNodeId = 0;
			link->outputPortId = 0;
			link->inputNodeId = 0;
			link->inputPortId = 0;
			link->output = info.output;
			link->input = info.input;
			if (!outLinks->AddItem(link))
				delete link;
		}
	}

	return B_OK;
}

