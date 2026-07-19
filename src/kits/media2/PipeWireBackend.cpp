/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PipeWireBackend.h"

#include <mutex>
#include <new>

#include <string.h>

#include <pipewire/filter.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/dict.h>

#include "FormatConversion.h"


namespace BPrivate { namespace media {


static PipeWireBackend* sInstance = NULL;
static std::once_flag   sInitOnce;
static bool             sPipeWireInited = false;


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
}


PipeWireBackend::~PipeWireBackend()
{
	_Teardown();
}


status_t
PipeWireBackend::_Init()
{
	if (!sPipeWireInited) {
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
				/* global        */ &PipeWireBackend::_GlobalThunk,
				/* global_remove */ &PipeWireBackend::_GlobalRemoveThunk
			};
			pw_registry_add_listener(fRegistry, &fRegistryListener,
				&kRegEvents, this);
		}
	}
	pw_thread_loop_unlock(fThreadLoop);

	if (fCore == NULL)
		return B_DEVICE_NOT_FOUND;  // pipewire daemon not running

	return B_OK;
}


// #pragma mark - registry listener


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

	if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
		return;
	const char* mediaClass = props != NULL
		? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : NULL;
	if (mediaClass == NULL)
		return;
	const bool isSink   = strcmp(mediaClass, "Audio/Sink") == 0;
	const bool isSource = strcmp(mediaClass, "Audio/Source") == 0;
	if (!isSink && !isSource)
		return;

	const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
	const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);

	DeviceInfo info;
	info.id         = id;
	info.name       = desc != NULL ? desc : (name != NULL ? name : "");
	info.nodeName   = name != NULL ? name : "";
	info.mediaClass = mediaClass;
	info.isSink     = isSink;
	info.isSource   = isSource;

	std::lock_guard<std::mutex> _(fDevicesLock);
	fDevices.push_back(info);
}


void
PipeWireBackend::_OnRegistryGlobalRemove(uint32 id)
{
	std::lock_guard<std::mutex> _(fDevicesLock);
	for (auto it = fDevices.begin(); it != fDevices.end(); ++it) {
		if (it->id == id) {
			fDevices.erase(it);
			return;
		}
	}
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
	// Value is a JSON-ish blob like {"name":"alsa_output.pci-0000_00_1b.0.analog-stereo"}.
	// Cheap extraction: find "name":"..." substring.
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
		// Fallback: bare string
		if (nodeName.empty() && value[0] != '{')
			nodeName = value;
	}
	std::lock_guard<std::mutex> _(self->fMetadataLock);
	if (strcmp(key, "default.audio.sink") == 0)
		self->fDefaultSinkName = nodeName;
	else if (strcmp(key, "default.audio.source") == 0)
		self->fDefaultSourceName = nodeName;
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


void
PipeWireBackend::_Teardown()
{
	if (fThreadLoop != NULL)
		pw_thread_loop_stop(fThreadLoop);

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
	std::call_once(sInitOnce, [] {
		PipeWireBackend* b = new(std::nothrow) PipeWireBackend();
		if (b == NULL)
			return;
		b->fInitStatus = b->_Init();
		if (b->fInitStatus != B_OK) {
			delete b;
			return;
		}
		sInstance = b;
	});
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
	const pw_stream_events* events, void* userdata)
{
	if (fLoop == NULL || events == NULL)
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


} } // namespace BPrivate::media
