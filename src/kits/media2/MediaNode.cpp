/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaNode.h>

#include <new>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <vector>

#include <Autolock.h>

#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <pipewire/link.h>
#include <pipewire/proxy.h>
#include <spa/param/props.h>

#include "PipeWireBackend.h"


using namespace BPrivate::media;


const struct pw_link_events BMediaNode::sLinkEvents = {
	PW_VERSION_LINK_EVENTS,
	.info = &BMediaNode::_OnLinkInfo
};


void
BMediaNode::_OnLinkInfo(void* data, const struct pw_link_info* info)
{
	NodeLinkInfo* linkInfo = (NodeLinkInfo*)data;

	if (info == NULL || linkInfo == NULL || linkInfo->owner == NULL)
		return;

	{
		BAutolock _(linkInfo->owner->fLinksLock);
		linkInfo->state = info->state;
	}

	if (info->state == PW_LINK_STATE_ERROR || info->state == PW_LINK_STATE_UNLINKED) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend != NULL)
			backend->_Broadcast('PWLC', pw_proxy_get_bound_id(linkInfo->proxy));
	}
}


BMediaNode::BMediaNode(const char* name, media_type type,
	media_client_kinds kinds)
	:
	BMediaClient(name, kinds),
	fFormat()
{
	fFormat.SetToDefault();
	fFormat.format.type = type;
}


BMediaNode::~BMediaNode()
{
	Stop();
}


status_t
BMediaNode::GetPreferredFormat(BMediaFormat* format) const
{
	if (format == NULL)
		return B_BAD_VALUE;
	*format = fFormat;
	return B_OK;
}


status_t
BMediaNode::SetFormat(const BMediaFormat& format)
{
	if (IsStarted())
		return B_NOT_ALLOWED;
	fFormat = format;
	return B_OK;
}


status_t
BMediaNode::Start()
{
	return BMediaClient::Start();
}


status_t
BMediaNode::Stop()
{
	return BMediaClient::Stop();
}


status_t
BMediaNode::_StartConnections(void* backendPtr)
{
	PipeWireBackend* backend = (PipeWireBackend*)backendPtr;
	if (backend == NULL)
		return B_BAD_VALUE;

	BString groupName(Name());
	groupName << "-group";

	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL)
			continue;

		BMediaFormat format = output->Format();
		if (format.Type() == B_MEDIA_NO_TYPE)
			format = fFormat;

		if (!format.IsRawAudio())
			return B_NOT_SUPPORTED;

		pw_direction direction = PW_DIRECTION_OUTPUT;
		pw_stream* stream = backend->CreateAndConnectStream(
			output->Name(), direction, format,
			_GetStreamEvents(), output);
		if (stream == NULL)
			return B_ERROR;

		output->fStream = stream;
	}

	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input == NULL)
			continue;

		BMediaFormat format = input->Format();
		if (format.Type() == B_MEDIA_NO_TYPE)
			format = fFormat;

		if (!format.IsRawAudio())
			return B_NOT_SUPPORTED;

		pw_direction direction = PW_DIRECTION_INPUT;
		pw_stream* stream = backend->CreateAndConnectStream(
			input->Name(), direction, format,
			_GetStreamEvents(), input);
		if (stream == NULL)
			return B_ERROR;

		input->fStream = stream;
	}

	return B_OK;
}


void
BMediaNode::_StopConnections()
{
	_DestroyLinks();

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return;

	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL && output->fStream != NULL) {
			backend->DestroyStream((pw_stream*)output->fStream);
			output->fStream = NULL;
		}
	}

	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL && input->fStream != NULL) {
			backend->DestroyStream((pw_stream*)input->fStream);
			input->fStream = NULL;
		}
	}
}


status_t
BMediaNode::Bind(BMediaInput* input, BMediaOutput* output)
{
	if (input == NULL || output == NULL)
		return B_BAD_VALUE;

	if (input->Client() != this || output->Client() != this)
		return B_BAD_VALUE;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	pw_core* core = backend->GetCore();
	if (core == NULL)
		return B_ERROR;

	return _CreateLink(output, input, core);
}


status_t
BMediaNode::Unbind(BMediaInput* input, BMediaOutput* output)
{
	if (input == NULL || output == NULL)
		return B_BAD_VALUE;

	if (input->Client() != this || output->Client() != this)
		return B_BAD_VALUE;

	if (output->fStream == NULL)
		return B_NOT_SUPPORTED;

	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	backend->Lock();
	status_t result = B_ENTRY_NOT_FOUND;
	{
		BAutolock _(fLinksLock);
		auto it = fLinks.find(output->fStream);
		if (it != fLinks.end()) {
			for (size_t i = 0; i < it->second.size(); i++) {
				auto infoIt = fLinkInfos.find(it->second[i]);
				if (infoIt != fLinkInfos.end()) {
					spa_hook_remove(&infoIt->second.listener);
					pw_proxy_destroy(infoIt->second.proxy);
					fLinkInfos.erase(infoIt);
				}
			}
			fLinks.erase(it);
			result = B_OK;
		}
	}
	backend->Unlock();

	if (result == B_OK) {
		input->fBinding = NULL;
		output->fBinding = NULL;
	}

	return result;
}


status_t
BMediaNode::_CreateLink(BMediaOutput* output, BMediaInput* input, pw_core* core)
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();
	if (backend == NULL)
		return B_DEVICE_NOT_FOUND;

	uint32 outNodeId = output->_GetNodeId();
	uint32 inNodeId = input->_GetNodeId();
	if (outNodeId == 0 || inNodeId == 0)
		return B_ERROR;

	uint32 outPortBuf[64], inPortBuf[64];
	uint32 outCount = output->_GetPortIds(outPortBuf, 64);
	uint32 inCount = input->_GetPortIds(inPortBuf, 64);
	if (outCount == 0 || inCount == 0)
		return B_ERROR;

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
		{
			BAutolock linksLock(fLinksLock);
			NodeLinkInfo& info = fLinkInfos[key];
			info.proxy = proxy;
			info.state = PW_LINK_STATE_INIT;
			info.owner = this;
			memset(&info.listener, 0, sizeof(info.listener));
			pw_link_add_listener((struct pw_link*)proxy, &info.listener,
				&sLinkEvents, &info);
		}

		createdKeys.push_back(key);
	}

	if (result == B_OK) {
		result = backend->RoundtripLocked();
	}

	if (result == B_OK) {
		BAutolock linksLock(fLinksLock);
		for (size_t j = 0; j < createdKeys.size(); j++) {
			auto it = fLinkInfos.find(createdKeys[j]);
			if (it != fLinkInfos.end() && it->second.state == PW_LINK_STATE_ERROR) {
				result = B_ERROR;
				break;
			}
		}
	}

	if (result != B_OK) {
		BAutolock linksLock(fLinksLock);
		for (size_t j = 0; j < createdKeys.size(); j++) {
			auto it = fLinkInfos.find(createdKeys[j]);
			if (it != fLinkInfos.end()) {
				spa_hook_remove(&it->second.listener);
				pw_proxy_destroy(it->second.proxy);
				fLinkInfos.erase(it);
			}
		}
		backend->Unlock();
		return result;
	}

	{
		BAutolock linksLock(fLinksLock);
		fLinks[output->fStream] = createdKeys;
	}
	backend->Unlock();

	input->fBinding = output;
	output->fBinding = input;

	return B_OK;
}


void
BMediaNode::_DestroyLinks()
{
	PipeWireBackend* backend = PipeWireBackend::GetInstance();

	if (backend != NULL)
		backend->Lock();
	{
		BAutolock _(fLinksLock);
		for (auto it = fLinkInfos.begin(); it != fLinkInfos.end(); ++it) {
			spa_hook_remove(&it->second.listener);
			pw_proxy_destroy(it->second.proxy);
		}
		fLinkInfos.clear();
		fLinks.clear();
	}
	if (backend != NULL)
		backend->Unlock();

	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL)
			input->fBinding = NULL;
	}

	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL)
			output->fBinding = NULL;
	}
}
