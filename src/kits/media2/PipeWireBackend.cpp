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
#include <spa/utils/dict.h>

#include <OS.h>
#include <Notification.h>

#include "FormatConversion.h"

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
	const bool isStreamOut = strncmp(mediaClass, "Stream/Output", 13) == 0;
	const bool isStreamIn  = strncmp(mediaClass, "Stream/Input",  12) == 0;

	if (isSink || isSource) {
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
		info.isSource   = isSource;

		{
			std::lock_guard<std::mutex> _(fDevicesLock);
			fDevices.push_back(info);
		}

		if (cardId != 0 && fCardWatches.find(cardId) == fCardWatches.end()) {
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
		_Broadcast(kMsgDevicesChanged, id);
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
		_Broadcast(kMsgStreamsChanged, id);
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

	if (deviceChanged) {
		_Broadcast(kMsgDevicesChanged, id);

		if (wasSink) {
			ShowNotification(kNotificationDeviceRemoved, "Audio Output Removed",
				deviceName.c_str(), id);
		} else {
			ShowNotification(kNotificationDeviceRemoved, "Audio Input Removed",
				deviceName.c_str(), id);
		}
	}

	if (streamChanged)
		_Broadcast(kMsgStreamsChanged, id);
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
	}
	if (changed)
		self->_Broadcast(kMsgDefaultChanged);
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


void
PipeWireBackend::_Teardown()
{
	if (fThreadLoop != NULL)
		pw_thread_loop_stop(fThreadLoop);

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


void
PipeWireBackend::Lock()
{
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
	const char* targetNodeName, bool captureSinkMonitor)
{
	if (fLoop == NULL || events == NULL)
		return NULL;
	if (captureSinkMonitor && direction != PW_DIRECTION_INPUT)
		return NULL;

	spa_audio_info_raw info;
	if (!BuildSPAAudioInfo(format, &info))
		return NULL;

	const char* category = (direction == PW_DIRECTION_OUTPUT)
		? "Playback" : "Capture";

	Lock();
	pw_properties* props = pw_properties_new(
		PW_KEY_MEDIA_TYPE,     "Audio",
		PW_KEY_MEDIA_CATEGORY, category,
		PW_KEY_MEDIA_ROLE,     "Music",
		PW_KEY_NODE_NAME,      name != NULL ? name : "vitruvian-media",
		NULL);
	if (targetNodeName != NULL)
		pw_properties_set(props, PW_KEY_TARGET_OBJECT, targetNodeName);
	if (captureSinkMonitor)
		pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
	pw_stream* stream = pw_stream_new_simple(fLoop,
		name != NULL ? name : "vitruvian-media",
		props, events, userdata);
	Unlock();
	if (stream == NULL)
		return NULL;

	uint8_t buf[1024];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
	const spa_pod* params[1];
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

	Lock();
	const int r = pw_stream_connect(stream,
		direction, PW_ID_ANY,
		(pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT
			| PW_STREAM_FLAG_MAP_BUFFERS
			| PW_STREAM_FLAG_RT_PROCESS),
		params, 1);
	Unlock();

	if (r < 0) {
		Lock();
		pw_stream_destroy(stream);
		Unlock();
		return NULL;
	}
	return stream;
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
PipeWireBackend::AddWatcher(const BMessenger& target)
{
	std::lock_guard<std::mutex> _(fWatchersLock);
	fWatchers.push_back(target);
}


void
PipeWireBackend::RemoveWatcher(const BMessenger& target)
{
	std::lock_guard<std::mutex> _(fWatchersLock);
	for (auto it = fWatchers.begin(); it != fWatchers.end(); ++it) {
		if (*it == target) {
			fWatchers.erase(it);
			return;
		}
	}
}


void
PipeWireBackend::_Broadcast(uint32 what, uint32 deviceId)
{
	std::vector<BMessenger> snapshot;
	{
		std::lock_guard<std::mutex> _(fWatchersLock);
		snapshot = fWatchers;
	}
	BMessage msg(what);
	if (deviceId != 0)
		msg.AddUInt32("device_id", deviceId);
	for (auto& m : snapshot)
		m.SendMessage(&msg);
}


std::vector<PipeWireBackend::StreamInfo>
PipeWireBackend::Streams()
{
	std::lock_guard<std::mutex> _(fStreamsLock);
	return fStreams;
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
		_Broadcast(kMsgStreamsChanged, streamId);
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
		_Broadcast(kMsgStreamsChanged, streamId);
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

	uint32 deviceId = 0;
	uint32 streamNodeId = 0;
	bool isOutput = false;

	{
		std::lock_guard<std::mutex> _(fDevicesLock);
		for (auto& d : fDevices) {
			if (d.id == outputNodeId) {
				deviceId = outputNodeId;
				streamNodeId = inputNodeId;
				isOutput = false;
				break;
			} else if (d.id == inputNodeId) {
				deviceId = inputNodeId;
				streamNodeId = outputNodeId;
				isOutput = true;
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
		_Broadcast(kMsgStreamsChanged, streamNodeId);

		ShowNotification(kNotificationDeviceConnected, "Audio Stream Connected",
			"Stream routed to audio device", streamNodeId);
	}
}

	void
PipeWireBackend::_OnLinkRemove(uint32 id)
{
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
		_Broadcast(kMsgStreamsChanged, streamNodeId);

		ShowNotification(kNotificationDeviceDisconnected, "Audio Stream Disconnected",
			"Stream unlinked from audio device", streamNodeId);
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
		_Broadcast(kMsgStreamsChanged, id);
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
		if (rs != B_OK) {
			fprintf(stderr, "nexus: SetDeviceVolume(%u): could not query "
				"active route (status=%s)\n", deviceId, strerror(rs));
			return rs;
		}

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
		spa_pod_builder_add(&b, SPA_PARAM_ROUTE_save, SPA_POD_Bool(true), 0);
		const spa_pod* routeParam
			= (const spa_pod*)spa_pod_builder_pop(&b, &routeFrame);
		if (routeParam == NULL)
			return B_NO_MEMORY;

		Lock();
		struct pw_device* device = (struct pw_device*)pw_registry_bind(
			fRegistry, cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE,
			0);
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
			fprintf(stderr, "nexus: SetDeviceVolume(%u): route not applied "
				"(read back %zu ch, first %.3f, wanted %.3f)\n",
				deviceId, verify.volumes.size(),
				verify.volumes.empty() ? -1.0f : verify.volumes[0],
				volumes[0]);
			return B_ERROR;
		}

		_Broadcast(kMsgDeviceVolumeChanged, deviceId);
		return B_OK;
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

	_Broadcast(kMsgDeviceVolumeChanged, deviceId);
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
		if (rs != B_OK) {
			fprintf(stderr, "nexus: SetDeviceMute(%u): could not query "
				"active route (status=%s)\n", deviceId, strerror(rs));
			return rs;
		}

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
		spa_pod_builder_add(&b, SPA_PARAM_ROUTE_save, SPA_POD_Bool(true), 0);
		const spa_pod* routeParam
			= (const spa_pod*)spa_pod_builder_pop(&b, &routeFrame);
		if (routeParam == NULL)
			return B_NO_MEMORY;

		Lock();
		struct pw_device* device = (struct pw_device*)pw_registry_bind(
			fRegistry, cardId, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE,
			0);
		if (device == NULL) {
			Unlock();
			return B_DEVICE_NOT_FOUND;
		}
		pw_device_set_param(device, SPA_PARAM_Route, 0, routeParam);

		RouteVolumeQuery verify = { wantDirection, -1, -1, {}, false, false,
			false };
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
			fprintf(stderr, "nexus: SetDeviceMute(%u): route not applied "
				"(haveMute=%d, mute=%d, wanted %d)\n",
				deviceId, verify.haveMute, verify.mute, mute);
			return B_ERROR;
		}

		_Broadcast(kMsgDeviceVolumeChanged, deviceId);
		return B_OK;
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

	_Broadcast(kMsgDeviceVolumeChanged, deviceId);
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
	notification_type haikuType;
	switch (type) {
		case kNotificationDeviceAdded:
		case kNotificationDeviceRemoved:
		case kNotificationDeviceConnected:
		case kNotificationDeviceDisconnected:
		case kNotificationDefaultChanged:
			haikuType = B_INFORMATION_NOTIFICATION;
			break;
		case kNotificationVolumeChanged:
			haikuType = B_PROGRESS_NOTIFICATION;
			break;
		default:
			haikuType = B_INFORMATION_NOTIFICATION;
			break;
	}
	BNotification notification(haikuType);
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

} }
