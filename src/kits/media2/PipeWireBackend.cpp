/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PipeWireBackend.h"

#include <algorithm>
#include <errno.h>
#include <math.h>
#include <mutex>
#include <new>
#include <stdio.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include <string.h>
#include <spa/param/props.h>
#include <spa/param/port-config.h>
#include <spa/param/profile.h>
#include <spa/param/route.h>
#include <spa/pod/iter.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <pipewire/filter.h>
#include <pipewire/port.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/type.h>

#include <media2/Controllable.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/dict.h>

#include <Application.h>
#include <ApplicationPrivate.h>
#include <OS.h>
#include <Notification.h>
#include <media2/MediaGraph.h>

#include "FormatConversion.h"
#include <ControllablePrivate.h>
#include <MediaGraphPrivate.h>

namespace BPrivate { namespace media {
static PipeWireBackend* sInstance = NULL;
static std::mutex       sInstanceLock;
static bool             sPipeWireInited = false;
static bigtime_t        sLastFailedAttempt = 0;
static const bigtime_t  kRetryCooldown = 5000000;

const char* const PipeWireBackend::kPeakMeterNodeName = "vitruvian-peak-meter";


PipeWireBackend::PipeWireBackend()
	:
	fThreadLoop(NULL),
	fLoop(NULL),
	fContext(NULL),
	fCore(NULL),
	fRegistry(NULL),
	fInitStatus(B_NO_INIT),
	fMetadata(NULL)
{
	memset(&fRegistryListener, 0, sizeof(fRegistryListener));
	memset(&fMetadataListener, 0, sizeof(fMetadataListener));
	memset(&fLinkListener, 0, sizeof(fLinkListener));
	memset(&fNodeListener, 0, sizeof(fNodeListener));
}


PipeWireBackend::~PipeWireBackend()
{
	_Teardown();
}


static void
ensure_pipewire_runtime_dir()
{
	if (getenv("PIPEWIRE_RUNTIME_DIR") != NULL)
		return;
	if (getenv("XDG_RUNTIME_DIR") != NULL)
		return;

	char path[64];
	snprintf(path, sizeof(path), "/run/user/%d", (int)getuid());

	struct stat st;
	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		debug_printf("PipeWireBackend: XDG_RUNTIME_DIR unset and %s is not "
			"usable (%s)\n", path, strerror(errno));
		return;
	}

	setenv("PIPEWIRE_RUNTIME_DIR", path, 1);
	debug_printf("PipeWireBackend: XDG_RUNTIME_DIR unset, falling back to "
		"PIPEWIRE_RUNTIME_DIR=%s\n", path);
}


status_t
PipeWireBackend::_Init()
{
	if (!sPipeWireInited) {
		ensure_pipewire_runtime_dir();
		pw_init(NULL, NULL);
		sPipeWireInited = true;
	}

	fThreadLoop = pw_thread_loop_new("vitruvian-media", NULL);
	if (fThreadLoop == NULL)
		return B_NO_MEMORY;
	fLoop = pw_thread_loop_get_loop(fThreadLoop);

	fContext = pw_context_new(fLoop, NULL, 0);
	if (fContext == NULL)
		return B_NO_MEMORY;

	if (pw_thread_loop_start(fThreadLoop) < 0)
		return B_ERROR;

	pw_thread_loop_lock(fThreadLoop);
	fCore = pw_context_connect(fContext, NULL, 0);
	if (fCore != NULL) {
		fRegistry = pw_core_get_registry(fCore, PW_VERSION_REGISTRY, 0);
		if (fRegistry != NULL) {
			// SPA event structs are positional C arrays with no designated
			// initializers in this codebase: a missing entry defaults to NULL
			// and inserting one shifts every following field onto the wrong
			// callback. Verify order against the header after any edit.
			static const pw_registry_events kRegEvents = {
				PW_VERSION_REGISTRY_EVENTS,
				&PipeWireBackend::_GlobalThunk,
				&PipeWireBackend::_GlobalRemoveThunk
			};
			pw_registry_add_listener(fRegistry, &fRegistryListener,
				&kRegEvents, this);

			static const pw_registry_events kLinkEvents = {
				PW_VERSION_REGISTRY_EVENTS,
				&PipeWireBackend::_LinkEventThunk,
				&PipeWireBackend::_LinkRemoveThunk
			};
			pw_registry_add_listener(fRegistry, &fLinkListener,
				&kLinkEvents, this);

			static const pw_registry_events kNodeEvents = {
				PW_VERSION_REGISTRY_EVENTS,
				&PipeWireBackend::_NodeEventThunk,
				&PipeWireBackend::_NodeRemoveThunk
			};
			pw_registry_add_listener(fRegistry, &fNodeListener,
				&kNodeEvents, this);
		}
	}
	pw_thread_loop_unlock(fThreadLoop);

	// Populate the registry before the first Devices() call.
	if (fCore != NULL) {
		pw_thread_loop_lock(fThreadLoop);
		RoundtripLocked(2000000);
		pw_thread_loop_unlock(fThreadLoop);
	}

	if (fCore == NULL) {
		const char* runtimeDir = getenv("PIPEWIRE_RUNTIME_DIR");
		if (runtimeDir == NULL)
			runtimeDir = getenv("XDG_RUNTIME_DIR");
		debug_printf("PipeWireBackend: pw_context_connect() failed, errno "
			"%d (%s), runtime dir \"%s\"\n", errno, strerror(errno),
			runtimeDir != NULL ? runtimeDir : "(unset)");
		return B_DEVICE_NOT_FOUND;
	}

	return B_OK;
}


void
PipeWireBackend::_GlobalThunk(void* data, uint32 id, uint32 perms,
	const char* type, uint32 version, const struct spa_dict* props)
{
	((PipeWireBackend*)data)->_OnRegistryGlobal(id, perms, type, version, props);
}


void
PipeWireBackend::_GlobalRemoveThunk(void* data, uint32 id)
{
	((PipeWireBackend*)data)->_OnRegistryGlobalRemove(id);
}


void
PipeWireBackend::_OnRegistryGlobal(uint32 id, uint32 /*perms*/,
	const char* type, uint32 /*version*/, const struct spa_dict* props)
{
	if (type == NULL)
		return;

	if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
		const char* metaName = props != NULL
			? spa_dict_lookup(props, "metadata.name") : NULL;
		if (metaName == NULL || strcmp(metaName, "default") != 0)
			return;
		if (fMetadata != NULL || fRegistry == NULL)
			return;
		pw_metadata* meta = (pw_metadata*)pw_registry_bind(fRegistry,
			id, type, PW_VERSION_METADATA, 0);
		if (meta == NULL)
			return;
		fMetadata = meta;
		static const pw_metadata_events kMetaEvents = {
			PW_VERSION_METADATA_EVENTS,
			&PipeWireBackend::_MetadataPropertyThunk
		};
		pw_metadata_add_listener(meta, &fMetadataListener, &kMetaEvents, this);
		return;
	}

	if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
		const char* nodeIdStr = props != NULL
			? spa_dict_lookup(props, PW_KEY_NODE_ID) : NULL;
		const char* dirStr = props != NULL
			? spa_dict_lookup(props, PW_KEY_PORT_DIRECTION) : NULL;
		if (nodeIdStr == NULL || dirStr == NULL)
			return;
		const char* portIdStr = spa_dict_lookup(props, PW_KEY_PORT_ID);
		const char* nameStr = spa_dict_lookup(props, PW_KEY_PORT_NAME);

		PortRegistryEntry entry;
		entry.nodeId = (uint32)strtoul(nodeIdStr, NULL, 10);
		entry.isOutput = strcmp(dirStr, "out") == 0;
		entry.portOrdinal = portIdStr != NULL
			? (uint32)strtoul(portIdStr, NULL, 10) : 0;
		entry.name = nameStr != NULL ? nameStr : "";

		{
			std::lock_guard<std::mutex> _(fPortsLock);
			fPorts[id] = entry;
		}

		if (fThreadLoop != NULL)
			pw_thread_loop_signal(fThreadLoop, false);
		return;
	}

	if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
		return;
	const char* mediaClass = props != NULL
		? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : NULL;
	if (mediaClass == NULL)
		return;
	const bool isSink   = strcmp(mediaClass, "Audio/Sink") == 0;
	const bool isSource = strcmp(mediaClass, "Audio/Source") == 0;
	const bool isVideo  = strcmp(mediaClass, "Video/Source") == 0;
	const bool isStreamOut = strncmp(mediaClass, "Stream/Output", 13) == 0;
	const bool isStreamIn  = strncmp(mediaClass, "Stream/Input",  12) == 0;

	if (isSink || isSource || isVideo) {
		const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
		const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
		const char* cardIdStr = props != NULL
			? spa_dict_lookup(props, "device.id") : NULL;
		uint32 cardId = 0;
		if (cardIdStr != NULL)
			cardId = strtoul(cardIdStr, NULL, 10);

		DeviceInfo info;
		info.id         = id;
		info.cardId     = cardId;
		info.name       = desc != NULL ? desc : (name != NULL ? name : "");
		info.nodeName   = name != NULL ? name : "";
		info.mediaClass = mediaClass;
		info.isSink     = isSink;
		info.isSource   = isSource && !isVideo;
		info.isVideo    = isVideo;
		info.stableId   = "";
		info.location   = "";

		if (isVideo) {
			// Prefer udev-backed serial; never persist /dev/videoN as the key.
			const char* serial = spa_dict_lookup(props, "device.serial");
			const char* busPath = spa_dict_lookup(props, "device.bus-path");
			const char* objectPath = spa_dict_lookup(props, "object.path");
			const char* v4l2Path = spa_dict_lookup(props, "api.v4l2.path");
			if (serial != NULL && serial[0] != '\0')
				info.stableId = std::string("serial:") + serial;
			else if (busPath != NULL && busPath[0] != '\0')
				info.stableId = std::string("bus:") + busPath;
			else if (objectPath != NULL && objectPath[0] != '\0')
				info.stableId = std::string("path:") + objectPath;
			else if (v4l2Path != NULL && v4l2Path[0] != '\0')
				info.stableId = std::string("v4l2:") + v4l2Path;
			else if (!info.nodeName.empty())
				info.stableId = std::string("node:") + info.nodeName;

			const char* deviceApi = spa_dict_lookup(props, "device.api");
			const char* loc = spa_dict_lookup(props, "api.libcamera.location");
			if (loc != NULL && loc[0] != '\0')
				info.location = loc;
			else if (deviceApi != NULL && strcmp(deviceApi, "libcamera") == 0)
				info.location = "libcamera";
			else
				info.location = "external";
		}

		{
			std::lock_guard<std::mutex> _(fDevicesLock);
			fDevices.push_back(info);
		}

		if (cardId != 0 && fCardWatches.find(cardId) == fCardWatches.end()) {
			// Bind runs on the loop thread (registry callback); the hook
			// lives inside the heap CardWatch, never a stack local (L2).
			pw_device* device = (pw_device*)pw_registry_bind(fRegistry,
				cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
			if (device != NULL) {
				CardWatch* watch = new(std::nothrow) CardWatch();
				if (watch == NULL) {
					pw_proxy_destroy((pw_proxy*)device);
				} else {
					watch->backend     = this;
					watch->cardId      = cardId;
					watch->proxy       = device;
					watch->lastProfile = -1;
					memset(&watch->hook, 0, sizeof(watch->hook));
					fCardWatches[cardId] = watch;

					static const pw_device_events kCardEvents = {
						PW_VERSION_DEVICE_EVENTS,
						NULL,
						&PipeWireBackend::_CardParamThunk
					};
					pw_device_add_listener(device, &watch->hook, &kCardEvents,
						watch);

					uint32_t ids[2] = { SPA_PARAM_Route, SPA_PARAM_Profile };
					pw_device_subscribe_params(device, ids, 2);
				}
			}
		}
		_Broadcast(BMediaGraph::B_MEDIA_CLIENT_ADDED, id);
		return;
	}

	if (isStreamOut || isStreamIn) {
		const char* nodeName = props != NULL
			? spa_dict_lookup(props, PW_KEY_NODE_NAME) : NULL;
		if (nodeName != NULL && strcmp(nodeName, kPeakMeterNodeName) == 0)
			return;

		const char* appName = props != NULL
			? spa_dict_lookup(props, PW_KEY_APP_NAME) : NULL;
		const char* mediaName = props != NULL
			? spa_dict_lookup(props, PW_KEY_MEDIA_NAME) : NULL;
		const char* targetNode = props != NULL
			? spa_dict_lookup(props, PW_KEY_NODE_TARGET) : NULL;

		StreamInfo s;
		s.id         = id;
		s.name       = (appName != NULL ? appName
			: (mediaName != NULL ? mediaName : "stream"));
		s.mediaClass = mediaClass;
		s.isOutput   = isStreamOut;
		s.deviceId   = 0;
		if (targetNode != NULL)
			s.deviceId = (uint32)strtoul(targetNode, NULL, 10);
		s.volume     = 1.0f;
		s.mute       = false;

		{
			std::lock_guard<std::mutex> _(fStreamsLock);
			fStreams.push_back(s);
		}
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, id);
		return;
	}

	if (strncmp(mediaClass, "Midi/", 5) == 0) {
		const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
		const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);

		MidiNodeInfo info;
		info.id         = id;
		info.name       = name != NULL ? name : "";
		info.description= desc != NULL ? desc : info.name;
		info.mediaClass = mediaClass;

		{
			std::lock_guard<std::mutex> _(fMidiNodesLock);
			fMidiNodes.push_back(info);
		}
		_Broadcast(kMsgMidiNodesChanged, id);
		return;
	}
}


void
PipeWireBackend::_OnRegistryGlobalRemove(uint32 id)
{
	{
		std::lock_guard<std::mutex> _(fPortsLock);
		if (fPorts.erase(id) > 0 && fThreadLoop != NULL)
			pw_thread_loop_signal(fThreadLoop, false);
	}

	bool deviceChanged = false;

	_DestroyCardWatch(id);

	bool streamChanged = false;
	std::string deviceName;
	bool wasSink = false;

	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto it = fDevices.begin(); it != fDevices.end(); ++it) {
			if (it->id == id) {
				deviceName = it->name;
				wasSink = it->isSink;
				fDevices.erase(it);
				deviceChanged = true;
				break;
			}
		}
	}

	if (!deviceChanged) {
		std::lock_guard<std::mutex> _(fStreamsLock);
		for (auto it = fStreams.begin(); it != fStreams.end(); ++it) {
			if (it->id == id) {
				fStreams.erase(it);
				streamChanged = true;
				break;
			}
		}
	}

	bool midiChanged = false;
	if (!deviceChanged && !streamChanged) {
		std::lock_guard<std::mutex> _(fMidiNodesLock);
		for (auto it = fMidiNodes.begin(); it != fMidiNodes.end(); ++it) {
			if (it->id == id) {
				fMidiNodes.erase(it);
				midiChanged = true;
				break;
			}
		}
	}

	if (deviceChanged) {
		_Broadcast(BMediaGraph::B_MEDIA_CLIENT_REMOVED, id);

		if (wasSink) {
			ShowNotification(kNotificationDeviceRemoved, "Audio Output Removed",
				deviceName.c_str(), id);
		} else {
			ShowNotification(kNotificationDeviceRemoved, "Audio Input Removed",
				deviceName.c_str(), id);
		}
	}

	if (streamChanged)
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, id);

	if (midiChanged)
		_Broadcast(kMsgMidiNodesChanged, id);
}


void
PipeWireBackend::_CardParamThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	CardWatch* watch = (CardWatch*)data;
	if (watch != NULL && watch->backend != NULL)
		watch->backend->_OnCardParam(watch, id, param);
}


void
PipeWireBackend::_OnCardParam(CardWatch* watch, uint32 id,
	const struct spa_pod* param)
{
	if (param == NULL)
		return;

	bool portChanged = false;
	bool profileChanged = false;

	if (id == SPA_PARAM_Route
			&& spa_pod_is_object_type(param, SPA_TYPE_OBJECT_ParamRoute)) {
		int32 index = -1;
		int32 routeDevice = -1;
		const struct spa_pod_prop* prop;
		SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
			if (prop->key == SPA_PARAM_ROUTE_index)
				spa_pod_get_int(&prop->value, &index);
			else if (prop->key == SPA_PARAM_ROUTE_device)
				spa_pod_get_int(&prop->value, &routeDevice);
		}
		if (index >= 0 && routeDevice >= 0) {
			auto it = watch->lastRoute.find(routeDevice);
			if (it == watch->lastRoute.end() || it->second != index) {
				watch->lastRoute[routeDevice] = index;

				portChanged = true;
			}
		}
	} else if (id == SPA_PARAM_Profile
			&& spa_pod_is_object_type(param, SPA_TYPE_OBJECT_ParamProfile)) {
		int32 index = -1;
		const struct spa_pod_prop* prop;
		SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
			if (prop->key == SPA_PARAM_PROFILE_index)
				spa_pod_get_int(&prop->value, &index);
		}
		if (index >= 0 && index != watch->lastProfile) {
			watch->lastProfile = index;
			profileChanged = true;
		}
	}

	if (!portChanged && !profileChanged)
		return;

	std::vector<uint32> nodeIds;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (const auto& d : fDevices) {
			if (d.cardId == watch->cardId)
				nodeIds.push_back(d.id);
		}
	}

	for (uint32 nodeId : nodeIds) {
		if (portChanged)
			_Broadcast(kMsgDevicePortChanged, nodeId);
		if (profileChanged)
			_Broadcast(kMsgDeviceProfileChanged, nodeId);
	}
}


void
PipeWireBackend::_DestroyCardWatch(uint32 cardId)
{
	// Caller holds the loop lock or runs after stop; never leave a listener
	// registered against a destroyed proxy.
	auto it = fCardWatches.find(cardId);
	if (it == fCardWatches.end())
		return;
	CardWatch* watch = it->second;
	fCardWatches.erase(it);

	spa_hook_remove(&watch->hook);
	if (watch->proxy != NULL)
		pw_proxy_destroy((pw_proxy*)watch->proxy);
	delete watch;
}


std::vector<PipeWireBackend::DeviceInfo>
PipeWireBackend::Devices()
{
	std::lock_guard<std::mutex> _(fDevicesLock);
	return fDevices;
}


int
PipeWireBackend::_MetadataPropertyThunk(void* data, uint32 /*subject*/,
	const char* key, const char* /*type*/, const char* value)
{
	PipeWireBackend* self = (PipeWireBackend*)data;
	if (key == NULL)
		return 0;

	std::string nodeName;
	if (value != NULL) {
		const char* p = strstr(value, "\"name\"");
		if (p != NULL) {
			p = strchr(p + 6, '"');
			if (p != NULL) {
				p++;
				const char* end = strchr(p, '"');
				if (end != NULL)
					nodeName.assign(p, end - p);
			}
		}

		if (nodeName.empty() && value[0] != '{')
			nodeName = value;
	}
	std::lock_guard<std::mutex> _(self->fMetadataLock);
	bool changed = false;
	if (strcmp(key, "default.audio.sink") == 0) {
		if (self->fDefaultSinkName != nodeName) {
			self->fDefaultSinkName = nodeName;
			changed = true;
		}
	} else if (strcmp(key, "default.audio.source") == 0) {
		if (self->fDefaultSourceName != nodeName) {
			self->fDefaultSourceName = nodeName;
			changed = true;
		}
	} else if (strcmp(key, "default.video.source") == 0) {
		if (self->fDefaultVideoSourceName != nodeName) {
			self->fDefaultVideoSourceName = nodeName;
			changed = true;
		}
	}
	if (changed)
		self->_Broadcast(BMediaGraph::B_MEDIA_DEFAULTS_CHANGED);
	return 0;
}


uint32
PipeWireBackend::GetDefaultSinkId()
{
	std::string want;
	{
		std::lock_guard<std::mutex> _(fMetadataLock);
		want = fDefaultSinkName;
	}
	if (want.empty())
		return 0;
	std::lock_guard<std::mutex> _(fDevicesLock);
	for (auto& d : fDevices) {
		if (d.isSink && d.nodeName == want)
			return d.id;
	}
	return 0;
}


uint32
PipeWireBackend::GetDefaultSourceId()
{
	std::string want;
	{
		std::lock_guard<std::mutex> _(fMetadataLock);
		want = fDefaultSourceName;
	}
	if (want.empty())
		return 0;
	std::lock_guard<std::mutex> _(fDevicesLock);
	for (auto& d : fDevices) {
		if (d.isSource && d.nodeName == want)
			return d.id;
	}
	return 0;
}


uint32
PipeWireBackend::GetDefaultVideoSourceId()
{
	std::string want;
	{
		std::lock_guard<std::mutex> _(fMetadataLock);
		want = fDefaultVideoSourceName;
	}
	if (want.empty())
		return 0;
	std::lock_guard<std::mutex> _(fDevicesLock);
	for (auto& d : fDevices) {
		if (d.isVideo && d.nodeName == want)
			return d.id;
	}
	return 0;
}


static status_t
SetDefaultMetadataNode(void* metadata, const char* key, const std::string& nodeName)
{
	if (metadata == NULL)
		return B_DEVICE_NOT_FOUND;
	std::string json = "{\"name\":\"" + nodeName + "\"}";
	int res = pw_metadata_set_property((pw_metadata*)metadata, PW_ID_CORE,
		key, "Spa:String:JSON", json.c_str());
	return res >= 0 ? B_OK : B_ERROR;
}


status_t
PipeWireBackend::SetDefaultSink(uint32 deviceId)
{
	std::string nodeName;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId && d.isSink) {
				nodeName = d.nodeName;
				break;
			}
		}
	}
	if (nodeName.empty())
		return B_BAD_VALUE;

	Lock();
	status_t status = SetDefaultMetadataNode(fMetadata,
		"default.audio.sink", nodeName);
	Unlock();
	return status;
}


status_t
PipeWireBackend::SetDefaultSource(uint32 deviceId)
{
	std::string nodeName;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId && d.isSource) {
				nodeName = d.nodeName;
				break;
			}
		}
	}
	if (nodeName.empty())
		return B_BAD_VALUE;

	Lock();
	status_t status = SetDefaultMetadataNode(fMetadata,
		"default.audio.source", nodeName);
	Unlock();
	return status;
}


status_t
PipeWireBackend::SetDefaultVideoSource(uint32 deviceId)
{
	std::string nodeName;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId && d.isVideo) {
				nodeName = d.nodeName;
				break;
			}
		}
	}
	if (nodeName.empty())
		return B_BAD_VALUE;

	Lock();
	status_t status = SetDefaultMetadataNode(fMetadata,
		"default.video.source", nodeName);
	Unlock();
	return status;
}


void
PipeWireBackend::_Teardown()
{
	// Loop thread is stopped first: destroy below must not race callbacks.
	if (fThreadLoop != NULL)
		pw_thread_loop_stop(fThreadLoop);

	while (!fNodeParamWatches.empty())
		_DestroyNodeParamWatch(fNodeParamWatches.begin()->first);

	while (!fCardWatches.empty())
		_DestroyCardWatch(fCardWatches.begin()->first);

	if (fMetadata != NULL) {
		pw_proxy_destroy((pw_proxy*)fMetadata);
		fMetadata = NULL;
	}
	if (fRegistry != NULL) {
		pw_proxy_destroy((pw_proxy*)fRegistry);
		fRegistry = NULL;
	}
	if (fCore != NULL) {
		pw_core_disconnect(fCore);
		fCore = NULL;
	}
	if (fContext != NULL) {
		pw_context_destroy(fContext);
		fContext = NULL;
	}
	if (fThreadLoop != NULL) {
		pw_thread_loop_destroy(fThreadLoop);
		fThreadLoop = NULL;
		fLoop = NULL;
	}
}


PipeWireBackend*
PipeWireBackend::GetInstance()
{
	std::lock_guard<std::mutex> lock(sInstanceLock);
	if (sInstance != NULL)
		return sInstance;

	bigtime_t now = system_time();
	if (sLastFailedAttempt != 0 && now - sLastFailedAttempt < kRetryCooldown)
		return NULL;

	PipeWireBackend* b = new(std::nothrow) PipeWireBackend();
	if (b == NULL)
		return NULL;
	b->fInitStatus = b->_Init();
	if (b->fInitStatus != B_OK) {
		delete b;
		sLastFailedAttempt = system_time();
		return NULL;
	}
	sInstance = b;
	return sInstance;
}


bigtime_t
PipeWireBackend::PerformanceTime()
{
	return system_time();
}


void
PipeWireBackend::Lock()
{
	// Always outermost. Bookkeeping locks (sLinksLock, fDevicesLock) nest
	// inside; never call RoundtripLocked from the loop thread while holding
	// a lock the loop thread itself needs.
	pw_thread_loop_lock(fThreadLoop);
}


void
PipeWireBackend::Unlock()
{
	pw_thread_loop_unlock(fThreadLoop);
}


pw_filter*
PipeWireBackend::CreateFilter(const char* name, pw_properties* props)
{
	if (fCore == NULL)
		return NULL;
	return pw_filter_new(fCore, name, props);
}


pw_stream*
PipeWireBackend::CreateAndConnectStream(const char* name,
	pw_direction direction, const BMediaFormat& format,
	const pw_stream_events* events, void* userdata,
	const char* targetNodeName, bool captureSinkMonitor, bool autoConnect,
	bool allowMjpeg)
{
	if (fLoop == NULL || events == NULL)
		return NULL;
	if (captureSinkMonitor && direction != PW_DIRECTION_INPUT)
		return NULL;

	const bool isVideo = format.IsRawVideo();
	const bool isAudio = format.IsRawAudio();
	if (!isVideo && !isAudio)
		return NULL;

	spa_audio_info_raw info;
	media_raw_video_format preferred;
	memset(&preferred, 0, sizeof(preferred));
	memset(&info, 0, sizeof(info));
	if (isVideo) {
		if (!format.IsRawVideo())
			return NULL;
		preferred = format.format.u.raw_video;
	} else {
		if (!BuildSPAAudioInfo(format, &info))
			return NULL;
	}

	const char* category = (direction == PW_DIRECTION_OUTPUT)
		? "Playback" : "Capture";

	Lock();
	pw_properties* props = pw_properties_new(
		PW_KEY_MEDIA_TYPE,     isVideo ? "Video" : "Audio",
		PW_KEY_MEDIA_CATEGORY, category,
		PW_KEY_MEDIA_ROLE,     isVideo ? "Camera" : "Music",
		PW_KEY_NODE_NAME,      name != NULL ? name : "vitruvian-media",
		NULL);
	if (targetNodeName != NULL)
		pw_properties_set(props, PW_KEY_TARGET_OBJECT, targetNodeName);
	// Graph-routed streams are linked explicitly; the session manager must not.
	if (!autoConnect)
		pw_properties_set(props, PW_KEY_NODE_AUTOCONNECT, "false");
	if (captureSinkMonitor)
		pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
	pw_stream* stream = pw_stream_new_simple(fLoop,
		name != NULL ? name : "vitruvian-media",
		props, events, userdata);
	Unlock();
	if (stream == NULL)
		return NULL;

	// Connect under the same lock as create/destroy (L3). The unlocked gap
	// above is only for building the pod; the stream is not yet in the graph.
	uint8_t buf[2048];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
	const spa_pod* params[3];
	uint32 paramCount = 1;
	// Video offers YUY2/BGRx/I420 with size and rate ranges; most UVC
	// webcams do not do BGRx only, so a single fixed format never links.
	if (isVideo) {
		// Exact preferred mode first, the full range as fallback.
		params[0] = BuildVideoEnumFormat(&b, preferred, true);
		params[1] = BuildVideoEnumFormat(&b, preferred);
		paramCount = 2;
		if (params[1] == NULL)
			params[0] = NULL;
		// Compressed fallback after raw, so raw wins when the device has it.
		if (allowMjpeg && direction == PW_DIRECTION_INPUT) {
			params[2] = BuildVideoEnumFormat(&b, preferred, false, true);
			if (params[2] != NULL)
				paramCount = 3;
		}
	} else
		params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
	if (params[0] == NULL) {
		Lock();
		pw_stream_destroy(stream);
		Unlock();
		return NULL;
	}

	Lock();
	const int r = pw_stream_connect(stream,
		direction, PW_ID_ANY,
		(pw_stream_flags)((autoConnect ? PW_STREAM_FLAG_AUTOCONNECT : 0)
			| PW_STREAM_FLAG_MAP_BUFFERS
			| PW_STREAM_FLAG_RT_PROCESS),
		params, paramCount);
	Unlock();

	if (r < 0) {
		Lock();
		pw_stream_destroy(stream);
		Unlock();
		return NULL;
	}
	return stream;
}


uint32
PipeWireBackend::VideoNodeIdByName(const char* nodeName)
{
	if (nodeName == NULL)
		return 0;
	std::lock_guard<std::mutex> _(fDevicesLock);
	for (auto& d : fDevices) {
		if (d.isVideo && d.nodeName == nodeName)
			return d.id;
	}
	return 0;
}


void
PipeWireBackend::DestroyStream(pw_stream* stream)
{
	if (stream == NULL)
		return;
	Lock();
	pw_stream_disconnect(stream);
	pw_stream_destroy(stream);
	Unlock();
}


void
PipeWireBackend::AddWatcher(const BMessenger& target, int32 notificationMask)
{
	std::lock_guard<std::mutex> _(fWatchersLock);
	for (auto& watcher : fWatchers) {
		if (watcher.messenger == target) {
			watcher.mask = notificationMask;
			return;
		}
	}
	Watcher watcher = { target, notificationMask };
	fWatchers.push_back(watcher);
}


void
PipeWireBackend::RemoveWatcher(const BMessenger& target)
{
	std::lock_guard<std::mutex> _(fWatchersLock);
	for (auto it = fWatchers.begin(); it != fWatchers.end(); ++it) {
		if (it->messenger == target) {
			fWatchers.erase(it);
			return;
		}
	}
}


// The public notification codes double as their own mask bit; private
// codes carry no bit, so every watcher receives those.
static bool
_IsNotified(int32 mask, uint32 what)
{
	switch (what) {
		case BMediaGraph::B_MEDIA_CLIENT_ADDED:
		case BMediaGraph::B_MEDIA_CLIENT_REMOVED:
		case BMediaGraph::B_MEDIA_DEFAULTS_CHANGED:
		case BMediaGraph::B_MEDIA_VOLUME_CHANGED:
		case BMediaGraph::B_MEDIA_STREAMS_CHANGED:
		case BMediaGraph::B_MEDIA_LINKS_CHANGED:
			return (mask & (int32)what) != 0;
		default:
			return true;
	}
}


void
PipeWireBackend::_Broadcast(uint32 what, uint32 deviceId)
{
	std::vector<Watcher> snapshot;
	{
		std::lock_guard<std::mutex> _(fWatchersLock);
		snapshot = fWatchers;
	}
	BMessage msg(what);
	if (deviceId != 0)
		msg.AddUInt32("device_id", deviceId);
	for (const auto& watcher : snapshot) {
		if (_IsNotified(watcher.mask, what))
			watcher.messenger.SendMessage(&msg);
	}
}


std::vector<PipeWireBackend::StreamInfo>
PipeWireBackend::Streams()
{
	std::lock_guard<std::mutex> _(fStreamsLock);
	return fStreams;
}


std::vector<PipeWireBackend::MidiNodeInfo>
PipeWireBackend::MidiNodes()
{
	std::lock_guard<std::mutex> _(fMidiNodesLock);
	return fMidiNodes;
}


status_t
PipeWireBackend::MoveStream(uint32 streamId, uint32 deviceId)
{
	bool found = false;
	{
		std::lock_guard<std::mutex> _(fStreamsLock);
		for (auto& s : fStreams) {
			if (s.id == streamId) {
				s.deviceId = deviceId;
				found = true;
				break;
			}
		}
	}
	if (found)
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, streamId);
	return found ? B_OK : B_BAD_VALUE;
}


status_t
PipeWireBackend::GetStreamVolume(uint32 streamId, float* outVolume,
	bool* outMute)
{
	if (outVolume == NULL)
		return B_BAD_VALUE;
	std::lock_guard<std::mutex> _(fStreamsLock);
	for (auto& s : fStreams) {
		if (s.id == streamId) {
			*outVolume = s.volume;
			if (outMute != NULL) *outMute = s.mute;
			return B_OK;
		}
	}
	return B_BAD_VALUE;
}


status_t
PipeWireBackend::SetStreamVolume(uint32 streamId, float volume, bool mute)
{
	bool found = false;
	{
		std::lock_guard<std::mutex> _(fStreamsLock);
		for (auto& s : fStreams) {
			if (s.id == streamId) {
				s.volume = volume;
				s.mute   = mute;
				found = true;
				break;
			}
		}
	}
	if (found)
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, streamId);
	return found ? B_OK : B_BAD_VALUE;
}


void
PipeWireBackend::_LinkEventThunk(void* data, uint32 id, uint32 permissions,
	const char* type, uint32 version, const struct spa_dict* props)
{
	((PipeWireBackend*)data)->_OnLinkEvent(id, permissions, type, version, props);
}


void
PipeWireBackend::_LinkRemoveThunk(void* data, uint32 id)
{
	((PipeWireBackend*)data)->_OnLinkRemove(id);
}


void
PipeWireBackend::_OnLinkEvent(uint32 id, uint32,
	const char* type, uint32, const struct spa_dict* props)
{
	if (type == NULL || strcmp(type, PW_TYPE_INTERFACE_Link) != 0)
		return;

	const char* outputNode = props != NULL ? spa_dict_lookup(props, "link.output.node") : NULL;
	const char* inputNode = props != NULL ? spa_dict_lookup(props, "link.input.node") : NULL;

	if (outputNode == NULL || inputNode == NULL)
		return;

	uint32 outputNodeId = (uint32)atoi(outputNode);
	uint32 inputNodeId = (uint32)atoi(inputNode);

	const char* outputPort = props != NULL
		? spa_dict_lookup(props, "link.output.port") : NULL;
	const char* inputPort = props != NULL
		? spa_dict_lookup(props, "link.input.port") : NULL;

	{
		std::lock_guard<std::mutex> _(fRegistryLinksLock);
		RegistryLinkInfo& entry = fRegistryLinks[id];
		entry.id = id;
		entry.outputNodeId = outputNodeId;
		entry.outputPortId = outputPort != NULL
			? (uint32)atoi(outputPort) : 0;
		entry.inputNodeId = inputNodeId;
		entry.inputPortId = inputPort != NULL ? (uint32)atoi(inputPort) : 0;
	}

	_Broadcast(BMediaGraph::B_MEDIA_LINKS_CHANGED, id);

	uint32 deviceId = 0;
	uint32 streamNodeId = 0;

	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == outputNodeId) {
				deviceId = outputNodeId;
				streamNodeId = inputNodeId;
				break;
			} else if (d.id == inputNodeId) {
				deviceId = inputNodeId;
				streamNodeId = outputNodeId;
				break;
			}
		}
	}

	if (deviceId == 0 || streamNodeId == 0)
		return;

	bool streamFound = false;
	{
		std::lock_guard<std::mutex> _(fStreamsLock);
		for (auto& stream : fStreams) {
			if (stream.id == streamNodeId) {
				stream.deviceId = deviceId;
				streamFound = true;
				break;
			}
		}
	}

	if (streamFound) {
		{
			std::lock_guard<std::mutex> _(fLinkToDeviceMapLock);
			fLinkToDeviceMap[id] = deviceId;
		}
		// Watchers get the broadcast; a desktop notification per app stream
		// link (two per stereo stream) flooded every player switch.
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, streamNodeId);
	}
}

void
PipeWireBackend::_OnLinkRemove(uint32 id)
{
	{
		std::lock_guard<std::mutex> _(fRegistryLinksLock);
		if (fRegistryLinks.erase(id) > 0)
			_Broadcast(BMediaGraph::B_MEDIA_LINKS_CHANGED, id);
	}

	uint32 deviceId = 0;
	uint32 streamNodeId = 0;

	{
		std::lock_guard<std::mutex> _(fLinkToDeviceMapLock);
		auto linkIt = fLinkToDeviceMap.find(id);
		if (linkIt == fLinkToDeviceMap.end())
			return;

		deviceId = linkIt->second;
		fLinkToDeviceMap.erase(linkIt);
	}

	{
		std::lock_guard<std::mutex> _(fStreamsLock);
		for (auto& stream : fStreams) {
			if (stream.deviceId == deviceId) {
				streamNodeId = stream.id;
				stream.deviceId = 0;
				break;
			}
		}
	}

	if (streamNodeId != 0) {
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, streamNodeId);
	}
}


void
PipeWireBackend::_NodeEventThunk(void* data, uint32 id, uint32 permissions,
	const char* type, uint32 version, const struct spa_dict* props)
{
	((PipeWireBackend*)data)->_OnNodeEvent(id, permissions, type, version, props);
}


void
PipeWireBackend::_NodeRemoveThunk(void* data, uint32 id)
{
	((PipeWireBackend*)data)->_OnNodeRemove(id);
}


void
PipeWireBackend::_OnNodeEvent(uint32 id, uint32,
	const char* type, uint32, const struct spa_dict* props)
{
	if (type == NULL || strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
		return;

	if (props == NULL)
		return;

	bool isDevice = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == id) {
				isDevice = true;
				break;
			}
		}
	}
	if (isDevice)
		return;

	const char* nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME);
	const char* appName = spa_dict_lookup(props, PW_KEY_APP_NAME);

	{
		if (nodeName != NULL)
			fNodeNames[id] = nodeName;
		if (appName != NULL)
			fNodeApps[id] = appName;
	}

	const char* volumeStr = spa_dict_lookup(props, "volume");
	const char* muteStr = spa_dict_lookup(props, "mute");

	bool volumeChanged = false;
	bool muteChanged = false;
	float newVolume = 1.0f;
	bool newMuted = false;

	{
		std::lock_guard<std::mutex> _(fStreamsLock);

		if (volumeStr != NULL) {
			newVolume = (float)atof(volumeStr);
			for (auto& stream : fStreams) {
				if (stream.id == id && stream.volume != newVolume) {
					stream.volume = newVolume;
					volumeChanged = true;
					break;
				}
			}
		}

		if (muteStr != NULL) {
			newMuted = (strcmp(muteStr, "true") == 0);
			for (auto& stream : fStreams) {
				if (stream.id == id && stream.mute != newMuted) {
					stream.mute = newMuted;
					muteChanged = true;
					break;
				}
			}
		}
	}

	if (volumeChanged || muteChanged)
		_Broadcast(BMediaGraph::B_MEDIA_STREAMS_CHANGED, id);
}


void
PipeWireBackend::_OnNodeRemove(uint32 id)
{
	fNodeNames.erase(id);
	fNodeApps.erase(id);
}


struct pw_node*
PipeWireBackend::_BindNode(uint32 id)
{
	if (fRegistry == NULL)
		return NULL;
	return (struct pw_node*)pw_registry_bind(fRegistry, id,
		PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
}


void
PipeWireBackend::_CoreDoneThunk(void* data, uint32 id, int seq)
{
	struct Ctx { bool done; int seq; pw_thread_loop* loop; };
	Ctx* ctx = (Ctx*)data;
	if (id == PW_ID_CORE && seq == ctx->seq) {
		ctx->done = true;
		pw_thread_loop_signal(ctx->loop, false);
	}
}


void
PipeWireBackend::_NodeParamThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	VolumeQuery* q = (VolumeQuery*)data;
	if (id != SPA_PARAM_Props || param == NULL)
		return;

	const spa_pod_prop* volProp = spa_pod_find_prop(param, NULL,
		SPA_PROP_channelVolumes);
	if (volProp != NULL) {
		uint32 n = 0;
		float* vals = (float*)spa_pod_get_array(&volProp->value, &n);
		if (vals != NULL && n > 0) {
			q->volumes.assign(vals, vals + n);
		}
	}

	const spa_pod_prop* muteProp = spa_pod_find_prop(param, NULL,
		SPA_PROP_mute);
	if (muteProp != NULL) {
		bool mute = false;
		if (spa_pod_get_bool(&muteProp->value, &mute) >= 0) {
			q->mute = mute;
			q->haveMute = true;
		}
	}

	q->done = true;
}


void
PipeWireBackend::_NodeControlParamThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	NodeControlQuery* q = (NodeControlQuery*)data;
	if (q == NULL || param == NULL)
		return;

	if (id == SPA_PARAM_PropInfo) {
		const spa_pod_prop* idProp = spa_pod_find_prop(param, NULL,
			SPA_PROP_INFO_id);
		if (idProp == NULL)
			return;
		uint32 propId = 0;
		if (spa_pod_get_id(&idProp->value, &propId) < 0)
			return;

		NodeControlInfo info;
		info.propId = propId;
		info.value = 0;
		info.min = 0;
		info.max = 0;
		info.step = 1;
		info.menuMask = 0;
		info.isBool = false;
		info.present = true;

		const spa_pod_prop* typeProp = spa_pod_find_prop(param, NULL,
			SPA_PROP_INFO_type);
		if (typeProp != NULL) {
			uint32 nVals = 0;
			uint32 choice = 0;
			const spa_pod* values = spa_pod_get_values(&typeProp->value,
				&nVals, &choice);
			if (values != NULL && nVals > 0) {
				const uint32 valType = SPA_POD_TYPE(values);
				if (valType == SPA_TYPE_Bool) {
					bool v = false;
					spa_pod_get_bool(values, &v);
					info.isBool = true;
					info.value = v ? 1 : 0;
					info.max = 1;
				} else if (choice == SPA_CHOICE_Step && nVals >= 4) {
					const int32* v = (const int32*)SPA_POD_BODY(values);
					info.value = v[0];
					info.min = v[1];
					info.max = v[2];
					info.step = v[3] != 0 ? v[3] : 1;
				} else if (choice == SPA_CHOICE_Range && nVals >= 3) {
					const int32* v = (const int32*)SPA_POD_BODY(values);
					info.value = v[0];
					info.min = v[1];
					info.max = v[2];
				} else if (choice == SPA_CHOICE_Enum && valType == SPA_TYPE_Int
					&& nVals >= 2) {
					// Menus: default first, then the items the device offers.
					const int32* v = (const int32*)SPA_POD_BODY(values);
					info.value = v[0];
					info.min = v[1];
					info.max = v[1];
					for (uint32 n = 1; n < nVals; n++) {
						if (v[n] >= 0 && v[n] < 32)
							info.menuMask |= 1u << v[n];
						info.min = v[n] < info.min ? v[n] : info.min;
						info.max = v[n] > info.max ? v[n] : info.max;
					}
				} else if (valType == SPA_TYPE_Int) {
					const int32* v = (const int32*)SPA_POD_BODY(values);
					info.value = v[0];
				}
			}
		}
		q->info.push_back(info);
		return;
	}

	if (id != SPA_PARAM_Props)
		return;

	// Fold current device values into the PropInfo list built above.
	for (size_t i = 0; i < q->info.size(); i++) {
		NodeControlInfo& c = q->info[i];
		const spa_pod_prop* p = spa_pod_find_prop(param, NULL, c.propId);
		if (p == NULL)
			continue;
		if (c.isBool) {
			bool v = false;
			if (spa_pod_get_bool(&p->value, &v) >= 0)
				c.value = v ? 1 : 0;
		} else {
			int32 v = 0;
			if (spa_pod_get_int(&p->value, &v) >= 0)
				c.value = v;
			else {
				float f = 0;
				if (spa_pod_get_float(&p->value, &f) >= 0)
					c.value = (int32)f;
			}
		}
	}
	q->done = true;
}


status_t
PipeWireBackend::QueryNodeControls(const char* nodeName,
	std::vector<NodeControlInfo>& out)
{
	if (nodeName == NULL || nodeName[0] == '\0')
		return B_BAD_VALUE;
	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 nodeId = 0;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.isVideo && d.nodeName == nodeName) {
				nodeId = d.id;
				break;
			}
		}
	}
	if (nodeId == 0)
		return B_ENTRY_NOT_FOUND;

	NodeControlQuery query = { {}, false };
	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook listener;
	memset(&listener, 0, sizeof(listener));
	static const pw_node_events kControlEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodeControlParamThunk
	};
	pw_node_add_listener(node, &listener, &kControlEvents, &query);
	pw_node_enum_params(node, 0, SPA_PARAM_PropInfo, 0, UINT32_MAX, NULL);
	pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&listener);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	out = query.info;
	return B_OK;
}


status_t
PipeWireBackend::SetNodeControls(const char* nodeName,
	const std::vector<NodeControlInfo>& props)
{
	if (nodeName == NULL || nodeName[0] == '\0' || props.empty())
		return B_BAD_VALUE;
	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 nodeId = 0;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.isVideo && d.nodeName == nodeName) {
				nodeId = d.id;
				break;
			}
		}
	}
	if (nodeId == 0)
		return B_ENTRY_NOT_FOUND;

	uint8_t buffer[1024];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	spa_pod_frame frame;
	spa_pod_builder_push_object(&b, &frame,
		SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	for (size_t i = 0; i < props.size(); i++) {
		if (props[i].isBool)
			spa_pod_builder_add(&b, props[i].propId,
				SPA_POD_Bool(props[i].value != 0), 0);
		else
			spa_pod_builder_add(&b, props[i].propId,
				SPA_POD_Int(props[i].value), 0);
	}
	const spa_pod* param = (const spa_pod*)spa_pod_builder_pop(&b, &frame);
	if (param == NULL)
		return B_NO_MEMORY;

	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}
	pw_node_set_param(node, SPA_PARAM_Props, 0, param);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();
	return B_OK;
}


void
PipeWireBackend::_RouteVolumeParamThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	RouteVolumeQuery* q = (RouteVolumeQuery*)data;
	if (id != SPA_PARAM_Route || param == NULL
			|| !spa_pod_is_object_type(param, SPA_TYPE_OBJECT_ParamRoute)) {
		return;
	}

	int32 index = -1;
	int32 direction = -1;
	int32 device = -1;
	const spa_pod* propsPod = NULL;

	const spa_pod_prop* prop;
	SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
		switch (prop->key) {
			case SPA_PARAM_ROUTE_index:
				spa_pod_get_int(&prop->value, &index);
				break;
			case SPA_PARAM_ROUTE_direction:
			{
				uint32_t dir = 0;
				if (spa_pod_get_id(&prop->value, &dir) >= 0)
					direction = (int32)dir;
				break;
			}
			case SPA_PARAM_ROUTE_device:
				spa_pod_get_int(&prop->value, &device);
				break;
			case SPA_PARAM_ROUTE_props:
				propsPod = &prop->value;
				break;
		}
	}

	if (index < 0 || direction != q->wantDirection)
		return;

	q->index = index;
	q->device = device;
	q->haveRoute = true;

	if (propsPod == NULL)
		return;

	const spa_pod_prop* volProp = spa_pod_find_prop(propsPod, NULL,
		SPA_PROP_channelVolumes);
	if (volProp != NULL) {
		uint32 n = 0;
		float* vals = (float*)spa_pod_get_array(&volProp->value, &n);
		if (vals != NULL && n > 0) {
			q->volumes.assign(vals, vals + n);
		}
	}
	const spa_pod_prop* muteProp = spa_pod_find_prop(propsPod, NULL,
		SPA_PROP_mute);
	if (muteProp != NULL) {
		bool mute = false;
		if (spa_pod_get_bool(&muteProp->value, &mute) >= 0) {
			q->mute = mute;
			q->haveMute = true;
		}
	}
}


status_t
PipeWireBackend::_QueryActiveRoute(uint32 cardId, int32 wantDirection,
	RouteVolumeQuery& outQuery)
{
	outQuery = { wantDirection, -1, -1, {}, false, false, false };

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	static const pw_device_events kEvents = {
		PW_VERSION_DEVICE_EVENTS,
		NULL,
		&PipeWireBackend::_RouteVolumeParamThunk
	};

	Lock();
	struct pw_device* device = (struct pw_device*)pw_registry_bind(fRegistry,
		cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
	if (device == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	pw_device_add_listener(device, &hook, &kEvents, &outQuery);
	pw_device_enum_params(device, 0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
	status_t rt = RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)device);
	Unlock();

	if (rt != B_OK)
		return rt;
	if (!outQuery.haveRoute)
		return B_ERROR;
	return B_OK;
}


status_t
PipeWireBackend::GetDevicePorts(uint32 deviceId,
	std::vector<DevicePort>& outPorts, int32* outActivePortId)
{
	outPorts.clear();
	if (outActivePortId != NULL) *outActivePortId = -1;

	uint32 cardId = 0;
	bool wantOutput = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				cardId = d.cardId;
				wantOutput = d.isSink;
				break;
			}
		}
	}
	if (cardId == 0)
		return B_BAD_VALUE;

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	const int32 wantDirection = wantOutput
		? (int32)SPA_DIRECTION_OUTPUT : (int32)SPA_DIRECTION_INPUT;

	struct RouteQuery {
		std::vector<DevicePort>	ports;
		int32					active;
		bool					haveActive;
		int32					wantDirection;
	} query = { {}, -1, false, wantDirection };

	static const pw_device_events kRouteEvents = {
		PW_VERSION_DEVICE_EVENTS,
		NULL,
		[](void* data, int, uint32 id, uint32, uint32,
				const struct spa_pod* param) {
			RouteQuery* q = (RouteQuery*)data;
			if (param == NULL
					|| !spa_pod_is_object_type(param,
						SPA_TYPE_OBJECT_ParamRoute)) {
				return;
			}

			const spa_pod_prop* prop;
			int32 index = -1;
			int32 direction = -1;
			bool available = true;

			SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
				switch (prop->key) {
					case SPA_PARAM_ROUTE_index:
						spa_pod_get_int(&prop->value, &index);
						break;
					case SPA_PARAM_ROUTE_direction:
					{
						uint32_t dir = 0;
						if (spa_pod_get_id(&prop->value, &dir) >= 0)
							direction = (int32)dir;
						break;
					}
					case SPA_PARAM_ROUTE_available:
					{
						uint32_t avail = SPA_PARAM_AVAILABILITY_yes;
						if (spa_pod_get_id(&prop->value, &avail) >= 0)
							available =
								(avail != SPA_PARAM_AVAILABILITY_no);
						break;
					}
				}
			}

			if (index < 0 || direction != q->wantDirection)
				return;

			if (id == SPA_PARAM_Route) {
				q->active = index;
				q->haveActive = true;
				return;
			}
			if (id != SPA_PARAM_EnumRoute)
				return;

			DevicePort port = { "", "", index, false };
			SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
				if (prop->key == SPA_PARAM_ROUTE_name) {
					const char* s = NULL;
					if (spa_pod_get_string(&prop->value, &s) >= 0 && s != NULL)
						port.name = s;
				} else if (prop->key == SPA_PARAM_ROUTE_description) {
					const char* s = NULL;
					if (spa_pod_get_string(&prop->value, &s) >= 0 && s != NULL)
						port.description = s;
				}
			}
			port.active = available;
			q->ports.push_back(port);
		},
	};

	Lock();
	struct pw_device* device = (struct pw_device*)pw_registry_bind(fRegistry,
		cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
	if (device == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	pw_device_add_listener(device, &hook, &kRouteEvents, &query);

	pw_device_enum_params(device, 0, SPA_PARAM_EnumRoute, 0, UINT32_MAX,
		NULL);
	pw_device_enum_params(device, 0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)device);
	Unlock();

	if (query.ports.empty())
		return B_NOT_SUPPORTED;

	outPorts = query.ports;
	for (auto& p : outPorts)
		p.active = query.haveActive && p.id == query.active;
	if (outActivePortId != NULL)
		*outActivePortId = query.haveActive ? query.active : -1;

	return B_OK;
}


status_t
PipeWireBackend::SetDevicePort(uint32 deviceId, int32 portId)
{
	if (portId < 0)
		return B_BAD_VALUE;

	uint32 cardId = 0;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				cardId = d.cardId;
				break;
			}
		}
	}
	if (cardId == 0)
		return B_BAD_VALUE;

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	bool wantOutput = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				wantOutput = d.isSink;
				break;
			}
		}
	}
	const int32 wantDirection = wantOutput
		? (int32)SPA_DIRECTION_OUTPUT : (int32)SPA_DIRECTION_INPUT;

	struct RouteDeviceQuery {
		int32	routeDevice;
		int32	active;
		bool	haveActive;
		int32	wantIndex;
		int32	wantDirection;
	} rdQuery = { 0, -1, false, portId, wantDirection };

	static const pw_device_events kRouteDeviceEvents = {
		PW_VERSION_DEVICE_EVENTS,
		NULL,
		[](void* data, int, uint32 id, uint32, uint32,
				const struct spa_pod* param) {
			RouteDeviceQuery* q = (RouteDeviceQuery*)data;
			if (param == NULL
					|| !spa_pod_is_object_type(param,
						SPA_TYPE_OBJECT_ParamRoute)) {
				return;
			}

			int32 index = -1;
			int32 direction = -1;
			const spa_pod_prop* prop;
			SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
				if (prop->key == SPA_PARAM_ROUTE_index)
					spa_pod_get_int(&prop->value, &index);
				else if (prop->key == SPA_PARAM_ROUTE_direction) {
					uint32_t dir = 0;
					if (spa_pod_get_id(&prop->value, &dir) >= 0)
						direction = (int32)dir;
				}
			}
			if (index < 0 || direction != q->wantDirection)
				return;

			if (id == SPA_PARAM_Route) {
				q->active = index;
				q->haveActive = true;
				return;
			}
			if (id != SPA_PARAM_EnumRoute || index != q->wantIndex)
				return;
			prop = spa_pod_find_prop(param, NULL, SPA_PARAM_ROUTE_device);
			if (prop != NULL)
				spa_pod_get_int(&prop->value, &q->routeDevice);
		},
	};

	Lock();
	struct pw_device* device = (struct pw_device*)pw_registry_bind(fRegistry,
		cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
	if (device == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	pw_device_add_listener(device, &hook, &kRouteDeviceEvents, &rdQuery);

	pw_device_enum_params(device, 0, SPA_PARAM_EnumRoute, 0, UINT32_MAX,
		NULL);
	RoundtripLocked();

	uint8_t buffer[128];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	spa_pod_frame frame;
	spa_pod_builder_push_object(&b, &frame, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
	spa_pod_builder_add(&b, SPA_PARAM_ROUTE_index, SPA_POD_Int(portId), 0);
	spa_pod_builder_add(&b, SPA_PARAM_ROUTE_device, SPA_POD_Int(rdQuery.routeDevice), 0);
	spa_pod_builder_add(&b, SPA_PARAM_ROUTE_save, SPA_POD_Bool(true), 0);
	spa_pod* pod = (spa_pod*)spa_pod_builder_pop(&b, &frame);
	pw_device_set_param(device, SPA_PARAM_Route, 0, pod);
	pw_device_enum_params(device, 0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)device);
	Unlock();

	if (!rdQuery.haveActive || rdQuery.active != portId)
		return B_ERROR;

	return B_OK;
}


status_t
PipeWireBackend::GetDeviceProfiles(uint32 deviceId,
	std::vector<DeviceProfile>& outProfiles, int32* outActiveIndex)
{
	outProfiles.clear();
	if (outActiveIndex != NULL) *outActiveIndex = -1;

	uint32 cardId = 0;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				cardId = d.cardId;
				break;
			}
		}
	}
	if (cardId == 0)
		return B_BAD_VALUE;

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	Lock();
	struct pw_device* device = (struct pw_device*)pw_registry_bind(fRegistry, cardId,
		PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
	if (device == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	struct ProfileQuery {
		std::vector<DeviceProfile> profiles;
		int32 activeIndex;
		bool haveActive;
	} query = { {}, -1, false };

	static const pw_device_events kDeviceEvents = {
		PW_VERSION_DEVICE_EVENTS,
		NULL,
		[](void* data, int, uint32 id, uint32 index, uint32,
				const struct spa_pod* param) {
			ProfileQuery* q = (ProfileQuery*)data;
			if (param == NULL)
				return;

			if (!spa_pod_is_object_type(param, SPA_TYPE_OBJECT_ParamProfile))
				return;

			if (id == SPA_PARAM_EnumProfile) {
				DeviceProfile prof = {};
				prof.index = index;
				spa_pod_object* obj = (spa_pod_object*)param;
				const struct spa_pod_prop* prop;
				SPA_POD_OBJECT_FOREACH(obj, prop) {
					if (prop->key == SPA_PARAM_PROFILE_index) {
						int32 idx = -1;
						if (spa_pod_get_int(&prop->value, &idx) >= 0
								&& idx >= 0)
							prof.index = idx;
					} else if (prop->key == SPA_PARAM_PROFILE_name) {
						const char* name = NULL;
						spa_pod_get_string(&prop->value, &name);
						if (name != NULL)
							prof.name = name;
					} else if (prop->key == SPA_PARAM_PROFILE_description) {
						const char* desc = NULL;
						spa_pod_get_string(&prop->value, &desc);
						if (desc != NULL)
							prof.description = desc;
					} else if (prop->key == SPA_PARAM_PROFILE_available) {
						uint32_t avail = SPA_PARAM_AVAILABILITY_unknown;
						if (spa_pod_get_id(&prop->value, &avail) >= 0)
							prof.available = (avail != SPA_PARAM_AVAILABILITY_no);
					}
				}
				q->profiles.push_back(prof);
			} else if (id == SPA_PARAM_Profile) {
				const struct spa_pod_prop* prop;
				SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
					if (prop->key == SPA_PARAM_PROFILE_index) {
						spa_pod_get_int(&prop->value, &q->activeIndex);
						q->haveActive = true;
					}
				}
			}
		},
	};

	spa_hook deviceListener;
	memset(&deviceListener, 0, sizeof(deviceListener));
	pw_device_add_listener(device, &deviceListener, &kDeviceEvents, &query);

	pw_device_enum_params(device, 0, SPA_PARAM_EnumProfile, 0, UINT32_MAX, NULL);
	pw_device_enum_params(device, 0, SPA_PARAM_Profile, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&deviceListener);
	pw_proxy_destroy((struct pw_proxy*)device);
	Unlock();

	if (query.profiles.empty())
		return B_NOT_SUPPORTED;

	outProfiles = query.profiles;
	if (outActiveIndex != NULL) {
		*outActiveIndex = query.haveActive ? query.activeIndex : -1;
	}

	return B_OK;
}


status_t
PipeWireBackend::SetDeviceProfile(uint32 deviceId, int32 index)
{
	if (index < 0)
		return B_BAD_VALUE;

	uint32 cardId = 0;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				cardId = d.cardId;
				break;
			}
		}
	}
	if (cardId == 0)
		return B_BAD_VALUE;

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	struct ProfileActiveQuery {
		int32	active;
		bool	haveActive;
	} profileQuery = { -1, false };

	static const pw_device_events kProfileEvents = {
		PW_VERSION_DEVICE_EVENTS,
		NULL,
		[](void* data, int, uint32 id, uint32, uint32,
				const struct spa_pod* param) {
			ProfileActiveQuery* q = (ProfileActiveQuery*)data;
			if (param == NULL
					|| !spa_pod_is_object_type(param,
						SPA_TYPE_OBJECT_ParamProfile)) {
				return;
			}
			if (id != SPA_PARAM_Profile)
				return;
			const spa_pod_prop* prop;
			SPA_POD_OBJECT_FOREACH((const spa_pod_object*)param, prop) {
				if (prop->key == SPA_PARAM_PROFILE_index) {
					int32 active = -1;
					if (spa_pod_get_int(&prop->value, &active) >= 0)
						q->active = active;
					q->haveActive = true;
				}
			}
		},
	};

	Lock();
	struct pw_device* device = (struct pw_device*)pw_registry_bind(fRegistry,
		cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
	if (device == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	pw_device_add_listener(device, &hook, &kProfileEvents, &profileQuery);

	uint8_t buffer[128];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	spa_pod_frame frame;
	spa_pod_builder_push_object(&b, &frame, SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile);
	spa_pod_builder_add(&b, SPA_PARAM_PROFILE_index, SPA_POD_Int(index), 0);
	spa_pod_builder_add(&b, SPA_PARAM_PROFILE_save, SPA_POD_Bool(true), 0);
	spa_pod* pod = (spa_pod*)spa_pod_builder_pop(&b, &frame);

	pw_device_set_param(device, SPA_PARAM_Profile, 0, pod);
	pw_device_enum_params(device, 0, SPA_PARAM_Profile, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)device);
	Unlock();

	if (!profileQuery.haveActive || profileQuery.active != index)
		return B_ERROR;

	return B_OK;
}


status_t
PipeWireBackend::GetDeviceVolume(uint32 deviceId,
	std::vector<float>& outVolumes, bool* outMute)
{
	outVolumes.clear();
	if (outMute != NULL) *outMute = false;

	bool found = false;
	uint32 cardId = 0;
	bool isSink = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				found = true;
				cardId = d.cardId;
				isSink = d.isSink;
				break;
			}
		}
	}
	if (!found)
		return B_BAD_VALUE;

	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	if (cardId != 0) {
		const int32 wantDirection = isSink
			? (int32)SPA_DIRECTION_OUTPUT : (int32)SPA_DIRECTION_INPUT;
		RouteVolumeQuery rq;
		status_t rs = _QueryActiveRoute(cardId, wantDirection, rq);
		if (rs == B_TIMED_OUT)
			return B_TIMED_OUT;
		if (rs == B_OK && (!rq.volumes.empty() || rq.haveMute)) {
			outVolumes = rq.volumes;
			if (outMute != NULL)
				*outMute = rq.haveMute ? rq.mute : false;
			return B_OK;
		}
	}

	VolumeQuery query = { {}, false, false, false };

	Lock();
	struct pw_node* node = _BindNode(deviceId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook nodeListener;
	memset(&nodeListener, 0, sizeof(nodeListener));
	static const pw_node_events kNodeParamEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodeParamThunk
	};
	pw_node_add_listener(node, &nodeListener, &kNodeParamEvents, &query);
	pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

	RoundtripLocked();

	spa_hook_remove(&nodeListener);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	if (!query.done || (query.volumes.empty() && !query.haveMute))
		return B_ERROR;

	outVolumes = query.volumes;
	if (outMute != NULL)
		*outMute = query.haveMute ? query.mute : false;
	return B_OK;
}


status_t
PipeWireBackend::SetDeviceVolume(uint32 deviceId,
	const float* volumes, size_t count)
{
	if (volumes == NULL || count == 0)
		return B_BAD_VALUE;

	bool found = false;
	uint32 cardId = 0;
	bool isSink = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				found = true;
				cardId = d.cardId;
				isSink = d.isSink;
				break;
			}
		}
	}
	if (!found)
		return B_BAD_VALUE;

	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	std::vector<float> replicated;
	if (count == 1) {
		std::vector<float> current;
		bool curMute = false;
		status_t qs = GetDeviceVolume(deviceId, current, &curMute);
		if (qs != B_OK || current.empty()) {
			fprintf(stderr, "nexus: SetDeviceVolume(%u): could not determine "
				"channel count, refusing to send a 1-element array\n",
				deviceId);
			return B_ERROR;
		}
		replicated.assign(current.size(), volumes[0]);
		volumes = replicated.data();
		count = replicated.size();
	}

	if (cardId != 0) {
		const int32 wantDirection = isSink
			? (int32)SPA_DIRECTION_OUTPUT : (int32)SPA_DIRECTION_INPUT;

		RouteVolumeQuery rq;
		status_t rs = _QueryActiveRoute(cardId, wantDirection, rq);
		if (rs == B_TIMED_OUT)
			return B_TIMED_OUT;
		// Only ALSA-style cards carry routes; Bluetooth and other
		// endpoint devices expose volume on the node itself, so fall
		// through to the Props path below like GetDeviceVolume does.
		if (rs == B_OK && rq.haveRoute) {
			uint8_t buffer[512];
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
			spa_pod_frame routeFrame, propsFrame;
			spa_pod_builder_push_object(&b, &routeFrame,
				SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
			spa_pod_builder_add(&b,
				SPA_PARAM_ROUTE_index,  SPA_POD_Int(rq.index),
				SPA_PARAM_ROUTE_device, SPA_POD_Int(rq.device),
				0);
			spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
			spa_pod_builder_push_object(&b, &propsFrame,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
			spa_pod_builder_add(&b,
				SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float),
					SPA_TYPE_Float, (uint32)count, volumes),
				0);
			spa_pod_builder_pop(&b, &propsFrame);
			spa_pod_builder_add(&b, SPA_PARAM_ROUTE_save, SPA_POD_Bool(true),
				0);
			const spa_pod* routeParam
				= (const spa_pod*)spa_pod_builder_pop(&b, &routeFrame);
			if (routeParam == NULL)
				return B_NO_MEMORY;

			Lock();
			struct pw_device* device = (struct pw_device*)pw_registry_bind(
				fRegistry, cardId, PW_TYPE_INTERFACE_Device,
				PW_VERSION_DEVICE, 0);
			if (device == NULL) {
				Unlock();
				return B_DEVICE_NOT_FOUND;
			}
			pw_device_set_param(device, SPA_PARAM_Route, 0, routeParam);

			RouteVolumeQuery verify;
			spa_hook hook;
			memset(&hook, 0, sizeof(hook));
			static const pw_device_events kVerifyEvents = {
				PW_VERSION_DEVICE_EVENTS,
				NULL,
				&PipeWireBackend::_RouteVolumeParamThunk
			};
			verify = { wantDirection, -1, -1, {}, false, false, false };
			pw_device_add_listener(device, &hook, &kVerifyEvents, &verify);
			pw_device_enum_params(device, 0, SPA_PARAM_Route, 0, UINT32_MAX,
				NULL);
			status_t rt = RoundtripLocked();
			spa_hook_remove(&hook);
			pw_proxy_destroy((struct pw_proxy*)device);
			Unlock();

			if (rt != B_OK) {
				fprintf(stderr, "nexus: SetDeviceVolume(%u): route verify "
					"timed out\n", deviceId);
				return rt;
			}

			bool applied = !verify.volumes.empty()
				&& fabsf(verify.volumes[0] - volumes[0]) < 0.01f;
			if (!applied) {
				fprintf(stderr, "nexus: SetDeviceVolume(%u): route not "
					"applied (read back %zu ch, first %.3f, wanted %.3f)\n",
					deviceId, verify.volumes.size(),
					verify.volumes.empty() ? -1.0f : verify.volumes[0],
					volumes[0]);
				return B_ERROR;
			}

			_Broadcast(BMediaGraph::B_MEDIA_VOLUME_CHANGED, deviceId);
			return B_OK;
		}
	}

	uint8_t buffer[512];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	const spa_pod* param = (const spa_pod*)spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
		SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float),
			SPA_TYPE_Float, (uint32)count, volumes));
	if (param == NULL)
		return B_NO_MEMORY;

	Lock();
	struct pw_node* node = _BindNode(deviceId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}
	pw_node_set_param(node, SPA_PARAM_Props, 0, param);

	VolumeQuery verify = { {}, false, false, false };
	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	static const pw_node_events kVerifyEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodeParamThunk
	};
	pw_node_add_listener(node, &hook, &kVerifyEvents, &verify);
	pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	bool applied = !verify.volumes.empty()
		&& fabsf(verify.volumes[0] - volumes[0]) < 0.01f;
	if (!applied) {
		fprintf(stderr, "nexus: SetDeviceVolume(%u): not applied "
			"(read back %zu ch, first %.3f, wanted %.3f)\n",
			deviceId, verify.volumes.size(),
			verify.volumes.empty() ? -1.0f : verify.volumes[0],
			volumes[0]);
		return B_ERROR;
	}

	_Broadcast(BMediaGraph::B_MEDIA_VOLUME_CHANGED, deviceId);
	return B_OK;
}


status_t
PipeWireBackend::SetDeviceMute(uint32 deviceId, bool mute)
{
	bool found = false;
	uint32 cardId = 0;
	bool isSink = false;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				found = true;
				cardId = d.cardId;
				isSink = d.isSink;
				break;
			}
		}
	}
	if (!found)
		return B_BAD_VALUE;

	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	if (cardId != 0) {
		const int32 wantDirection = isSink
			? (int32)SPA_DIRECTION_OUTPUT : (int32)SPA_DIRECTION_INPUT;

		RouteVolumeQuery rq;
		status_t rs = _QueryActiveRoute(cardId, wantDirection, rq);
		if (rs == B_TIMED_OUT)
			return B_TIMED_OUT;
		// Same route-less fallback as SetDeviceVolume: Bluetooth and
		// other endpoint devices carry mute on the node Props.
		if (rs == B_OK && rq.haveRoute) {
			uint8_t buffer[256];
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
			spa_pod_frame routeFrame, propsFrame;
			spa_pod_builder_push_object(&b, &routeFrame,
				SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
			spa_pod_builder_add(&b,
				SPA_PARAM_ROUTE_index,  SPA_POD_Int(rq.index),
				SPA_PARAM_ROUTE_device, SPA_POD_Int(rq.device),
				0);
			spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
			spa_pod_builder_push_object(&b, &propsFrame,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
			spa_pod_builder_add(&b, SPA_PROP_mute, SPA_POD_Bool(mute), 0);
			spa_pod_builder_pop(&b, &propsFrame);
			spa_pod_builder_add(&b, SPA_PARAM_ROUTE_save, SPA_POD_Bool(true),
				0);
			const spa_pod* routeParam
				= (const spa_pod*)spa_pod_builder_pop(&b, &routeFrame);
			if (routeParam == NULL)
				return B_NO_MEMORY;

			Lock();
			struct pw_device* device = (struct pw_device*)pw_registry_bind(
				fRegistry, cardId, PW_TYPE_INTERFACE_Device,
				PW_VERSION_DEVICE, 0);
			if (device == NULL) {
				Unlock();
				return B_DEVICE_NOT_FOUND;
			}
			pw_device_set_param(device, SPA_PARAM_Route, 0, routeParam);

			RouteVolumeQuery verify = { wantDirection, -1, -1, {}, false,
				false, false };
			spa_hook hook;
			memset(&hook, 0, sizeof(hook));
			static const pw_device_events kVerifyEvents = {
				PW_VERSION_DEVICE_EVENTS,
				NULL,
				&PipeWireBackend::_RouteVolumeParamThunk
			};
			pw_device_add_listener(device, &hook, &kVerifyEvents, &verify);
			pw_device_enum_params(device, 0, SPA_PARAM_Route, 0, UINT32_MAX,
				NULL);
			status_t rt = RoundtripLocked();
			spa_hook_remove(&hook);
			pw_proxy_destroy((struct pw_proxy*)device);
			Unlock();

			if (rt != B_OK) {
				fprintf(stderr, "nexus: SetDeviceMute(%u): route verify "
					"timed out\n", deviceId);
				return rt;
			}

			if (!verify.haveMute || verify.mute != mute) {
				fprintf(stderr, "nexus: SetDeviceMute(%u): route not "
					"applied (haveMute=%d, mute=%d, wanted %d)\n",
					deviceId, verify.haveMute, verify.mute, mute);
				return B_ERROR;
			}

			_Broadcast(BMediaGraph::B_MEDIA_VOLUME_CHANGED, deviceId);
			return B_OK;
		}
	}

	uint8_t buffer[256];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	const spa_pod* param = (const spa_pod*)spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
		SPA_PROP_mute, SPA_POD_Bool(mute));
	if (param == NULL)
		return B_NO_MEMORY;

	Lock();
	struct pw_node* node = _BindNode(deviceId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}
	pw_node_set_param(node, SPA_PARAM_Props, 0, param);

	VolumeQuery verify = { {}, false, false, false };
	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	static const pw_node_events kVerifyEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodeParamThunk
	};
	pw_node_add_listener(node, &hook, &kVerifyEvents, &verify);
	pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);
	RoundtripLocked();
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	if (!verify.haveMute || verify.mute != mute) {
		fprintf(stderr, "nexus: SetDeviceMute(%u): not applied "
			"(haveMute=%d, mute=%d, wanted %d)\n",
			deviceId, verify.haveMute, verify.mute, mute);
		return B_ERROR;
	}

	_Broadcast(BMediaGraph::B_MEDIA_VOLUME_CHANGED, deviceId);
	return B_OK;
}


void
PipeWireBackend::_TestToneProcessThunk(void* data)
{
	TestTone* tone = (TestTone*)data;
	pw_stream* stream = tone->stream;
	pw_buffer* b = pw_stream_dequeue_buffer(stream);
	if (b == NULL || b->buffer == NULL || b->buffer->datas == NULL
		|| b->buffer->datas[0].data == NULL) {
		if (b != NULL)
			pw_stream_queue_buffer(stream, b);
		return;
	}

	spa_data& d = b->buffer->datas[0];
	const uint32 channels = (uint32)tone->channelCount;
	const uint32 stride = (uint32)(sizeof(float) * channels);
	uint32 frames = d.maxsize / stride;
	if (b->requested != 0 && (uint32)b->requested < frames)
		frames = (uint32)b->requested;

	float* dst = (float*)d.data;
	double phase = tone->phase.load(std::memory_order_relaxed);
	const double inc = tone->phaseIncrement;
	for (uint32 i = 0; i < frames; i++) {
		float sample = (float)(sin(phase) * 0.25);
		phase += inc;
		if (phase > 2.0 * M_PI)
			phase -= 2.0 * M_PI;
		for (uint32 c = 0; c < channels; c++) {
			dst[i * channels + c] =
				(tone->channel < 0 || (int)c == tone->channel) ? sample : 0.0f;
		}
	}
	tone->phase.store(phase, std::memory_order_relaxed);

	d.chunk->offset = 0;
	d.chunk->size = frames * stride;
	d.chunk->stride = (int32)stride;
	pw_stream_queue_buffer(stream, b);
}


status_t
PipeWireBackend::TestSpeaker(uint32 deviceId, TestChannel channel, uint32 durationMs)
{
	std::string nodeName;
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				nodeName = d.nodeName;
				break;
			}
		}
	}
	if (nodeName.empty())
		return B_BAD_VALUE;

	{
		std::lock_guard<std::mutex> _(fTestTonesLock);
		if (fTestTones.find(deviceId) != fTestTones.end())
			return B_BUSY;
	}

	const uint32 kSampleRate = 48000;
	const uint32 kChannels = 2;
	media_raw_audio_format raw;
	memset(&raw, 0, sizeof(raw));
	raw.frame_rate    = (float)kSampleRate;
	raw.channel_count = kChannels;
	raw.format        = media_raw_audio_format::B_AUDIO_FLOAT;
	raw.byte_order     = B_MEDIA_HOST_ENDIAN;
	raw.buffer_size    = 0;
	BMediaFormat format(raw);

	TestTone* tone = new(std::nothrow) TestTone();
	if (tone == NULL)
		return B_NO_MEMORY;
	tone->stream = NULL;
	tone->phase.store(0.0, std::memory_order_relaxed);
	tone->phaseIncrement = 2.0 * M_PI * 440.0 / kSampleRate;
	tone->channel = (channel == kTestChannelAll) ? -1 : (int)channel;
	tone->channelCount = (int)kChannels;

	static const pw_stream_events kToneEvents = {
		PW_VERSION_STREAM_EVENTS,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		&PipeWireBackend::_TestToneProcessThunk,
		NULL,
	};

	pw_stream* stream = CreateAndConnectStream("vitruvian-test-tone",
		PW_DIRECTION_OUTPUT, format, &kToneEvents, tone, nodeName.c_str());
	if (stream == NULL) {
		delete tone;
		return B_ERROR;
	}
	tone->stream = stream;

	{
		std::lock_guard<std::mutex> _(fTestTonesLock);
		fTestTones[deviceId] = tone;
	}

	tone->stopper = std::thread([this, deviceId, durationMs]() {
		usleep((useconds_t)durationMs * 1000);
		_StopTestSpeakerLocked(deviceId);
	});
	tone->stopper.detach();

	return B_OK;
}


void
PipeWireBackend::_StopTestSpeakerLocked(uint32 deviceId)
{
	TestTone* tone = NULL;
	{
		std::lock_guard<std::mutex> _(fTestTonesLock);
		auto it = fTestTones.find(deviceId);
		if (it == fTestTones.end())
			return;
		tone = it->second;
		fTestTones.erase(it);
	}
	DestroyStream(tone->stream);
	delete tone;
}


status_t
PipeWireBackend::StopTestSpeaker(uint32 deviceId)
{
	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		bool found = false;
		for (auto& d : fDevices) {
			if (d.id == deviceId) {
				found = true;
				break;
			}
		}
		if (!found)
			return B_BAD_VALUE;
	}

	bool wasActive;
	{
		std::lock_guard<std::mutex> _(fTestTonesLock);
		wasActive = fTestTones.find(deviceId) != fTestTones.end();
	}
	_StopTestSpeakerLocked(deviceId);
	return wasActive ? B_OK : B_OK;
}


void
PipeWireBackend::ShowNotification(NotificationType type, const char* title,
	const char* message, uint32 deviceId)
{
	// BNotification needs a live be_app with an app_server connection;
	// during shutdown or in headless teams posting one crashes.
	if (be_app == NULL || be_app->Thread() < B_OK
		|| BApplication::Private::ServerLink() == NULL) {
		fprintf(stderr, "media2: %s: %s\n", title, message);
		return;
	}

	// Volume is a state change, not an operation with a progress bar;
	// watchers already receive 'PWVC'. Progress type would need SetProgress.
	notification_type legacyType;
	switch (type) {
		case kNotificationDeviceAdded:
		case kNotificationDeviceRemoved:
		case kNotificationDeviceConnected:
		case kNotificationDeviceDisconnected:
		case kNotificationDefaultChanged:
		case kNotificationVolumeChanged:
			legacyType = B_INFORMATION_NOTIFICATION;
			break;
		default:
			legacyType = B_INFORMATION_NOTIFICATION;
			break;
	}
	BNotification notification(legacyType);
	notification.SetTitle(title);
	notification.SetContent(message);
	notification.SetGroup("AudioDevices");

	if (deviceId != 0) {
		char deviceIdStr[32];
		snprintf(deviceIdStr, sizeof(deviceIdStr), "%u", deviceId);
		notification.SetMessageID(deviceIdStr);
	}

	notification.Send();
}


status_t
PipeWireBackend::RoundtripLocked(bigtime_t timeoutUsecs)
{
	if (fThreadLoop == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	// Self-deadlock guard: the loop thread is the one that would deliver
	// `done`; waiting on it from there never returns (L6).
	if (pw_thread_loop_in_thread(fThreadLoop))
		return B_NOT_ALLOWED;

	const bigtime_t deadline = system_time() + timeoutUsecs;

	spa_hook coreListener;
	memset(&coreListener, 0, sizeof(coreListener));
	static const pw_core_events kCoreEvents = {
		PW_VERSION_CORE_EVENTS,
		NULL,
		&PipeWireBackend::_CoreDoneThunk,
		NULL,
		NULL,
		NULL
	};

	struct { bool done; int seq; pw_thread_loop* loop; } thunkCtx
		= { false, 0, fThreadLoop };
	pw_core_add_listener(fCore, &coreListener, &kCoreEvents, &thunkCtx);
	thunkCtx.seq = pw_core_sync(fCore, PW_ID_CORE, 0);

	while (!thunkCtx.done) {
		int64 remainingSec = (deadline - system_time() + 999999) / 1000000;
		if (remainingSec <= 0)
			break;
		pw_thread_loop_timed_wait(fThreadLoop, (int)remainingSec);
	}
	spa_hook_remove(&coreListener);

	return thunkCtx.done ? B_OK : B_TIMED_OUT;
}


status_t
PipeWireBackend::ResolveNodePorts(uint32 nodeId, pw_direction direction,
	std::vector<uint32>& outPortIds, bigtime_t timeoutUsecs)
{
	outPortIds.clear();

	if (fThreadLoop == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	// Blocks the caller; never from the loop thread or an RT callback (L6).
	if (pw_thread_loop_in_thread(fThreadLoop))
		return B_NOT_ALLOWED;

	const bool wantOutput = (direction == PW_DIRECTION_OUTPUT);
	const bigtime_t deadline = system_time() + timeoutUsecs;

	struct Found {
		uint32	globalId;
		uint32	ordinal;
	};
	std::vector<Found> found;

	Lock();

	if (RoundtripLocked(timeoutUsecs) != B_OK) {
		Unlock();
		return B_TIMED_OUT;
	}

	for (;;) {
		found.clear();
		{
			std::lock_guard<std::mutex> _(fPortsLock);
			for (auto& kv : fPorts) {
				if (kv.second.nodeId == nodeId
					&& kv.second.isOutput == wantOutput) {
					found.push_back({ kv.first, kv.second.portOrdinal });
				}
			}
		}

		if (!found.empty())
			break;

		int64 remainingSec = (deadline - system_time() + 999999) / 1000000;
		if (remainingSec <= 0) {
			Unlock();
			return B_TIMED_OUT;
		}
		pw_thread_loop_timed_wait(fThreadLoop, (int)remainingSec);
	}
	Unlock();

	std::sort(found.begin(), found.end(),
		[](const Found& a, const Found& b) { return a.ordinal < b.ordinal; });

	for (auto& f : found)
		outPortIds.push_back(f.globalId);

	return B_OK;
}


std::vector<PipeWireBackend::ForeignNodeInfo>
PipeWireBackend::ForeignNodes()
{
	std::vector<ForeignNodeInfo> nodes;
	std::lock_guard<std::mutex> _(fDevicesLock);

	for (const auto& device : fDevices) {
		ForeignNodeInfo info;
		info.id = device.id;
		info.name = device.nodeName;
		info.description = device.name;
		info.mediaClass = device.mediaClass;
		info.portCount = 0;
		info.outputPorts = device.isSink ? 0 : 2;
		info.inputPorts = device.isSink ? 2 : 0;
		nodes.push_back(info);
	}

	for (const auto& stream : fStreams) {
		ForeignNodeInfo info;
		info.id = stream.id;
		info.name = stream.name;
		info.description = stream.name;
		info.mediaClass = stream.mediaClass;
		info.portCount = 1;
		info.outputPorts = stream.isOutput ? 1 : 0;
		info.inputPorts = stream.isOutput ? 0 : 1;
		nodes.push_back(info);
	}

	return nodes;
}


std::vector<PipeWireBackend::ForeignPortInfo>
PipeWireBackend::ForeignPorts(uint32 nodeId)
{
	std::vector<ForeignPortInfo> ports;

	std::lock_guard<std::mutex> _(fPortsLock);
	for (const auto& kv : fPorts) {
		if (kv.second.nodeId != nodeId)
			continue;

		ForeignPortInfo port;
		port.id = kv.first;
		port.nodeId = kv.second.nodeId;
		port.name = kv.second.name;
		port.direction = kv.second.isOutput ? "out" : "in";
		port.format = "";
		ports.push_back(port);
	}
	return ports;
}


std::vector<PipeWireBackend::RegistryLinkInfo>
PipeWireBackend::Links()
{
	std::lock_guard<std::mutex> _(fRegistryLinksLock);
	std::vector<RegistryLinkInfo> links;
	links.reserve(fRegistryLinks.size());
	for (const auto& kv : fRegistryLinks)
		links.push_back(kv.second);
	return links;
}


status_t
PipeWireBackend::DestroyLink(uint32 linkId)
{
	if (linkId == 0)
		return B_BAD_VALUE;
	if (fRegistry == NULL || fThreadLoop == NULL)
		return B_DEVICE_NOT_FOUND;

	{
		std::lock_guard<std::mutex> _(fRegistryLinksLock);
		if (fRegistryLinks.find(linkId) == fRegistryLinks.end())
			return B_ENTRY_NOT_FOUND;
	}

	Lock();
	pw_registry_destroy(fRegistry, linkId);
	RoundtripLocked(500000);
	Unlock();
	return B_OK;
}


void
PipeWireBackend::_DestroyNodeParamWatch(uint32 nodeId)
{
	NodeParamWatch* watch = NULL;
	{
		std::lock_guard<std::mutex> _(fNodeParamWatchesLock);
		auto it = fNodeParamWatches.find(nodeId);
		if (it == fNodeParamWatches.end())
			return;
		watch = it->second;
		fNodeParamWatches.erase(it);
	}
	if (watch == NULL)
		return;
	Lock();
	if (watch->proxy != NULL) {
		spa_hook_remove(&watch->hook);
		pw_proxy_destroy((struct pw_proxy*)watch->proxy);
	}
	Unlock();
	delete watch;
}


void
PipeWireBackend::_NodeParamWatchThunk(void* data, int seq, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	NodeParamWatch* watch = (NodeParamWatch*)data;
	if (watch != NULL && watch->backend != NULL)
		watch->backend->_OnNodeParamWatch(watch, id, param);
}


void
PipeWireBackend::_OnNodeParamWatch(NodeParamWatch* watch, uint32 id,
	const struct spa_pod* param)
{
	if (param == NULL || id != SPA_PARAM_Props)
		return;

	std::vector<BControllable*> targets;
	{
		std::lock_guard<std::mutex> _(fNodeParamWatchesLock);
		targets = watch->targets;
	}
	for (size_t i = 0; i < targets.size(); i++) {
		if (targets[i] != NULL)
			ControllablePrivate::ApplyProps(targets[i], param);
	}
}


status_t
PipeWireBackend::WatchNodeParams(uint32 nodeId, BControllable* target)
{
	if (nodeId == 0 || target == NULL)
		return B_BAD_VALUE;
	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	NodeParamWatch* watch = NULL;
	bool created = false;
	{
		std::lock_guard<std::mutex> _(fNodeParamWatchesLock);
		auto it = fNodeParamWatches.find(nodeId);
		if (it != fNodeParamWatches.end()) {
			watch = it->second;
			for (size_t i = 0; i < watch->targets.size(); i++) {
				if (watch->targets[i] == target)
					return B_OK;
			}
		} else {
			watch = new(std::nothrow) NodeParamWatch();
			if (watch == NULL)
				return B_NO_MEMORY;
			watch->backend = this;
			watch->nodeId = nodeId;
			watch->proxy = NULL;
			memset(&watch->hook, 0, sizeof(watch->hook));
			fNodeParamWatches[nodeId] = watch;
			created = true;
		}
		watch->targets.push_back(target);
	}

	if (!created)
		return B_OK;

	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		UnwatchNodeParams(nodeId, target);
		return B_DEVICE_NOT_FOUND;
	}

	static const pw_node_events kWatchEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodeParamWatchThunk
	};
	pw_node_add_listener(node, &watch->hook, &kWatchEvents, watch);
	uint32_t ids[1] = { SPA_PARAM_Props };
	pw_node_subscribe_params(node, ids, 1);
	watch->proxy = node;
	Unlock();

	return B_OK;
}


status_t
PipeWireBackend::UnwatchNodeParams(uint32 nodeId, BControllable* target)
{
	if (nodeId == 0)
		return B_BAD_VALUE;

	NodeParamWatch* dead = NULL;
	{
		std::lock_guard<std::mutex> _(fNodeParamWatchesLock);
		auto it = fNodeParamWatches.find(nodeId);
		if (it == fNodeParamWatches.end())
			return B_OK;
		NodeParamWatch* watch = it->second;
		for (size_t i = 0; i < watch->targets.size(); i++) {
			if (watch->targets[i] == target) {
				watch->targets.erase(watch->targets.begin() + i);
				break;
			}
		}
		if (watch->targets.empty()) {
			fNodeParamWatches.erase(it);
			dead = watch;
		}
	}

	if (dead != NULL) {
		Lock();
		if (dead->proxy != NULL) {
			spa_hook_remove(&dead->hook);
			pw_proxy_destroy((struct pw_proxy*)dead->proxy);
		}
		Unlock();
		delete dead;
	}
	return B_OK;
}


void
PipeWireBackend::_ParsePropInfoPod(const struct spa_pod* pod,
	NodePropInfo& outInfo)
{
	outInfo = NodePropInfo();
	if (pod == NULL || !spa_pod_is_object(pod))
		return;

	const spa_pod_object* object = (const spa_pod_object*)pod;
	const spa_pod_prop* prop;
	SPA_POD_OBJECT_FOREACH(object, prop) {
		switch (prop->key) {
			case SPA_PROP_INFO_id:
			{
				uint32_t id = 0;
				if (spa_pod_get_id(&prop->value, &id) >= 0)
					outInfo.id = id;
				break;
			}
			case SPA_PROP_INFO_name:
			{
				const char* name = NULL;
				if (spa_pod_get_string(&prop->value, &name) >= 0
						&& name != NULL)
					outInfo.name = name;
				break;
			}
			case SPA_PROP_INFO_type:
			{
				const struct spa_pod* typePod = &prop->value;
				// Choice pods wrap the default and alternatives.
				if (spa_pod_is_object(typePod)
						&& SPA_POD_OBJECT_TYPE(typePod)
							== SPA_TYPE_Object) {
					const spa_pod_object* typeObj
						= (const spa_pod_object*)typePod;
					const spa_pod_prop* tp;
					SPA_POD_OBJECT_FOREACH(typeObj, tp) {
						if (tp->key == SPA_CHOICE_None
								|| tp->key == 0) {
							typePod = &tp->value;
							break;
						}
					}
				}
				outInfo.valueType = SPA_POD_TYPE(typePod);
				if (spa_pod_is_object(typePod)
						&& SPA_POD_OBJECT_TYPE(typePod) == SPA_TYPE_Object) {
					const spa_pod_object* rangeObj
						= (const spa_pod_object*)typePod;
					const spa_pod_prop* rp;
					SPA_POD_OBJECT_FOREACH(rangeObj, rp) {
						if (rp->key == SPA_CHOICE_Range
								|| rp->key == SPA_CHOICE_Step) {
							const spa_pod_object* rangePods
								= (const spa_pod_object*)&rp->value;
							// Range is typically a Pod array or object of floats.
							// Try to read min/max from a nested object.
							if (spa_pod_is_object(&rp->value)) {
								const spa_pod_object* inner
									= (const spa_pod_object*)&rp->value;
								const spa_pod_prop* ip;
								float vals[3] = { 0, 0, 0 };
								int n = 0;
								SPA_POD_OBJECT_FOREACH(inner, ip) {
									if (n < 3
											&& spa_pod_get_float(&ip->value,
												&vals[n]) >= 0)
										n++;
								}
								if (n >= 2) {
									outInfo.hasRange = true;
									outInfo.minValue = vals[0];
									outInfo.maxValue = vals[1];
									if (n >= 3)
										outInfo.step = vals[2];
								}
							} else if (spa_pod_is_array(&rp->value)) {
								uint32_t n = 0;
								const float* f
									= (const float*)spa_pod_get_array(
										&rp->value, &n);
								if (f != NULL && n >= 2) {
									outInfo.hasRange = true;
									outInfo.minValue = f[0];
									outInfo.maxValue = f[1];
									if (n >= 3)
										outInfo.step = f[2];
								}
							}
							(void)rangePods;
						}
					}
				}
				break;
			}
			case SPA_PROP_INFO_labels:
			{
				// Labels are a Struct of (value, name) pairs.
				if (!spa_pod_is_object(&prop->value))
					break;
				const spa_pod_object* labels
					= (const spa_pod_object*)&prop->value;
				const spa_pod_prop* lp;
				uint32 pendingId = 0;
				bool havePending = false;
				SPA_POD_OBJECT_FOREACH(labels, lp) {
					if (lp->key % 2 == 0 || !havePending) {
						// First of pair is the value.
						uint32_t id = 0;
						int32_t i = 0;
						if (spa_pod_get_id(&lp->value, &id) >= 0)
							pendingId = id;
						else if (spa_pod_get_int(&lp->value, &i) >= 0)
							pendingId = (uint32)i;
						else if (spa_pod_get_float(&lp->value,
								(float*)&pendingId) >= 0)
							; // keep raw bits
						havePending = true;
					} else {
						const char* name = NULL;
						if (spa_pod_get_string(&lp->value, &name) >= 0
								&& name != NULL) {
							outInfo.labels.push_back(
								std::make_pair(pendingId,
									std::string(name)));
						}
						havePending = false;
					}
				}
				break;
			}
			default:
				break;
		}
	}
}


bool
PipeWireBackend::_ValueFromPod(const struct spa_pod* pod, uint32& outType,
	std::vector<uint8_t>& outBytes)
{
	if (pod == NULL)
		return false;
	outType = SPA_POD_TYPE(pod);
	const char* str = NULL;
	float f = 0.0f;
	int32_t i = 0;
	bool b = false;
	if (spa_pod_get_string(pod, &str) >= 0 && str != NULL) {
		size_t len = strlen(str) + 1;
		outBytes.resize(len);
		memcpy(outBytes.data(), str, len);
		return true;
	}
	if (spa_pod_get_float(pod, &f) >= 0) {
		outBytes.resize(sizeof(float));
		memcpy(outBytes.data(), &f, sizeof(float));
		return true;
	}
	if (spa_pod_get_int(pod, &i) >= 0) {
		outBytes.resize(sizeof(int32_t));
		memcpy(outBytes.data(), &i, sizeof(int32_t));
		return true;
	}
	if (spa_pod_get_bool(pod, &b) >= 0) {
		outBytes.resize(sizeof(bool));
		memcpy(outBytes.data(), &b, sizeof(bool));
		return true;
	}
	return false;
}


void
PipeWireBackend::_ParsePropsPod(const struct spa_pod* pod,
	std::vector<NodePropValue>& outValues)
{
	outValues.clear();
	if (pod == NULL || !spa_pod_is_object(pod))
		return;
	if (SPA_POD_OBJECT_TYPE(pod) != SPA_TYPE_OBJECT_Props
			&& SPA_POD_OBJECT_TYPE(pod) != SPA_TYPE_Object)
		return;

	const spa_pod_object* object = (const spa_pod_object*)pod;
	const spa_pod_prop* prop;
	SPA_POD_OBJECT_FOREACH(object, prop) {
		NodePropValue v;
		v.id = prop->key;
		if (_ValueFromPod(&prop->value, v.valueType, v.bytes))
			outValues.push_back(v);
	}
}


const struct spa_pod*
PipeWireBackend::_PodFromValue(const NodePropValue& value,
	void* buffer, size_t bufferSize)
{
	if (buffer == NULL || bufferSize < 64)
		return NULL;
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, bufferSize);
	const struct spa_pod* pod = NULL;
	switch (value.valueType) {
		case SPA_TYPE_Float:
		{
			float f = 0.0f;
			if (value.bytes.size() >= sizeof(float))
				memcpy(&f, value.bytes.data(), sizeof(float));
			pod = (const struct spa_pod*)spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
				value.id, SPA_POD_Float(f));
			break;
		}
		case SPA_TYPE_Bool:
		{
			bool bv = false;
			if (value.bytes.size() >= sizeof(bool))
				memcpy(&bv, value.bytes.data(), sizeof(bool));
			pod = (const struct spa_pod*)spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
				value.id, SPA_POD_Bool(bv));
			break;
		}
		case SPA_TYPE_Int:
		case SPA_TYPE_Id:
		{
			int32_t iv = 0;
			if (value.bytes.size() >= sizeof(int32_t))
				memcpy(&iv, value.bytes.data(), sizeof(int32_t));
			pod = (const struct spa_pod*)spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
				value.id, SPA_POD_Int(iv));
			break;
		}
		case SPA_TYPE_String:
		{
			const char* s = value.bytes.empty()
				? "" : (const char*)value.bytes.data();
			pod = (const struct spa_pod*)spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
				value.id, SPA_POD_String(s));
			break;
		}
		default:
			break;
	}
	return pod;
}


void
PipeWireBackend::_PropInfoThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	PropInfoQuery* q = (PropInfoQuery*)data;
	if (q == NULL)
		return;
	if (id == SPA_PARAM_PropInfo && param != NULL) {
		NodePropInfo info;
		_ParsePropInfoPod(param, info);
		if (info.id != 0 || !info.name.empty())
			q->infos.push_back(info);
	} else if (id == SPA_PARAM_Props && param != NULL) {
		q->done = true;
	}
}


void
PipeWireBackend::_NodePropsThunk(void* data, int, uint32 id,
	uint32, uint32, const struct spa_pod* param)
{
	NodePropsQuery* q = (NodePropsQuery*)data;
	if (q == NULL)
		return;
	if (id != SPA_PARAM_Props || param == NULL)
		return;
	_ParsePropsPod(param, q->values);
	q->done = true;
}


status_t
PipeWireBackend::QueryNodePropInfo(uint32 nodeId,
	std::vector<NodePropInfo>& outInfos, bigtime_t timeoutUsecs)
{
	outInfos.clear();
	if (nodeId == 0)
		return B_BAD_VALUE;
	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	// Prefer locally published info for filters we created.
	{
		std::lock_guard<std::mutex> _(fPublishedParamsLock);
		auto it = fPublishedInfos.find(nodeId);
		if (it != fPublishedInfos.end() && !it->second.empty()) {
			outInfos = it->second;
			return B_OK;
		}
	}

	PropInfoQuery query;
	query.done = false;

	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	static const pw_node_events kInfoEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_PropInfoThunk
	};
	pw_node_add_listener(node, &hook, &kInfoEvents, &query);
	pw_node_enum_params(node, 0, SPA_PARAM_PropInfo, 0, UINT32_MAX, NULL);
	status_t result = RoundtripLocked(timeoutUsecs);
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	if (result != B_OK)
		return result;
	outInfos = query.infos;
	return outInfos.empty() ? B_ENTRY_NOT_FOUND : B_OK;
}


status_t
PipeWireBackend::QueryNodeProps(uint32 nodeId,
	std::vector<NodePropValue>& outValues, bigtime_t timeoutUsecs)
{
	outValues.clear();
	if (nodeId == 0)
		return B_BAD_VALUE;
	if (fRegistry == NULL || fCore == NULL)
		return B_DEVICE_NOT_FOUND;

	{
		std::lock_guard<std::mutex> _(fPublishedParamsLock);
		auto it = fPublishedValues.find(nodeId);
		if (it != fPublishedValues.end() && !it->second.empty()) {
			outValues = it->second;
			return B_OK;
		}
	}

	NodePropsQuery query;
	query.done = false;

	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}

	spa_hook hook;
	memset(&hook, 0, sizeof(hook));
	static const pw_node_events kPropsEvents = {
		PW_VERSION_NODE_EVENTS,
		NULL,
		&PipeWireBackend::_NodePropsThunk
	};
	pw_node_add_listener(node, &hook, &kPropsEvents, &query);
	pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);
	status_t result = RoundtripLocked(timeoutUsecs);
	spa_hook_remove(&hook);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;
	outValues = query.values;
	return outValues.empty() ? B_ENTRY_NOT_FOUND : B_OK;
}


status_t
PipeWireBackend::SetNodeProp(uint32 nodeId, uint32 propId,
	uint32 valueType, const void* value, size_t size)
{
	if (nodeId == 0 || value == NULL || size == 0)
		return B_BAD_VALUE;
	if (fRegistry == NULL)
		return B_DEVICE_NOT_FOUND;

	NodePropValue v;
	v.id = propId;
	v.valueType = valueType;
	v.bytes.assign((const uint8_t*)value, (const uint8_t*)value + size);

	uint8_t buffer[512];
	const struct spa_pod* param = _PodFromValue(v, buffer, sizeof(buffer));
	if (param == NULL)
		return B_BAD_VALUE;

	Lock();
	struct pw_node* node = _BindNode(nodeId);
	if (node == NULL) {
		Unlock();
		return B_DEVICE_NOT_FOUND;
	}
	pw_node_set_param(node, SPA_PARAM_Props, 0, param);

	// Keep the published cache in sync so QueryNodeProps sees our write
	// even when the server-side filter does not echo Props back.
	{
		std::lock_guard<std::mutex> _(fPublishedParamsLock);
		std::vector<NodePropValue>& vals = fPublishedValues[nodeId];
		bool found = false;
		for (size_t i = 0; i < vals.size(); i++) {
			if (vals[i].id == propId) {
				vals[i] = v;
				found = true;
				break;
			}
		}
		if (!found)
			vals.push_back(v);
	}

	// Verify with a bounded round-trip when the node might answer.
	status_t rt = RoundtripLocked(1000000);
	pw_proxy_destroy((struct pw_proxy*)node);
	Unlock();

	if (rt == B_TIMED_OUT)
		return B_OK;
	return rt;
}


status_t
PipeWireBackend::PublishFilterParams(struct pw_filter* filter,
	const std::vector<NodePropInfo>& infos,
	const std::vector<NodePropValue>& values)
{
	if (filter == NULL)
		return B_BAD_VALUE;

	uint32 nodeId = pw_filter_get_node_id(filter);
	if (nodeId == 0)
		return B_ERROR;

	{
		std::lock_guard<std::mutex> _(fPublishedParamsLock);
		fPublishedInfos[nodeId] = infos;
		fPublishedValues[nodeId] = values;
	}

	Lock();

	// Push current Props so foreign clients see values via enum_params.
	std::vector<const struct spa_pod*> pods;
	std::vector<uint8_t> storage;
	storage.reserve(values.size() * 128);
	for (size_t i = 0; i < values.size(); i++) {
		size_t offset = storage.size();
		storage.resize(offset + 256);
		const struct spa_pod* pod = _PodFromValue(values[i],
			storage.data() + offset, 256);
		if (pod != NULL)
			pods.push_back(pod);
	}
	if (!pods.empty())
		pw_filter_update_params(filter, NULL, pods.data(),
			(uint32_t)pods.size());

	// Also publish PropInfo so pw-cli and our BindNode can discover them.
	std::vector<uint8_t> infoStorage;
	std::vector<const struct spa_pod*> infoPods;
	infoStorage.reserve(infos.size() * 256);
	for (size_t i = 0; i < infos.size(); i++) {
		size_t offset = infoStorage.size();
		infoStorage.resize(offset + 512);
		spa_pod_builder b = SPA_POD_BUILDER_INIT(
			infoStorage.data() + offset, 512);
		spa_pod_frame frame;
		const struct spa_pod* pod = NULL;
		if (spa_pod_builder_push_object(&b, &frame,
				SPA_TYPE_OBJECT_PropInfo, SPA_PARAM_PropInfo) >= 0) {
			spa_pod_builder_add(&b,
				SPA_PROP_INFO_id, SPA_POD_Id(infos[i].id),
				SPA_PROP_INFO_name, SPA_POD_String(infos[i].name.c_str()),
				0);
			if (infos[i].hasRange && infos[i].valueType == SPA_TYPE_Float) {
				spa_pod_builder_prop(&b, SPA_PROP_INFO_type, 0);
				spa_pod_frame typeFrame;
				if (spa_pod_builder_push_object(&b, &typeFrame,
						SPA_TYPE_Object, SPA_CHOICE_Range) >= 0) {
					spa_pod_builder_add(&b,
						SPA_CHOICE_None,
							SPA_POD_Float(infos[i].minValue),
						0);
					// Range alternatives as a simple float pair via Choice.
					spa_pod_builder_pop(&b, &typeFrame);
				}
			}
			pod = (const struct spa_pod*)spa_pod_builder_pop(&b, &frame);
		}
		if (pod != NULL)
			infoPods.push_back(pod);
	}
	if (!infoPods.empty())
		pw_filter_update_params(filter, NULL, infoPods.data(),
			(uint32_t)infoPods.size());

	Unlock();
	return B_OK;
}


} }
