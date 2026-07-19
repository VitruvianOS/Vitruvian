/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaGraph.h>

#include <mutex>
#include <new>

#include <Message.h>
#include <ObjectList.h>

#include "PipeWireBackend.h"


using namespace BPrivate::media;


static BMediaGraph* sGraphInstance = NULL;
static std::once_flag sGraphOnce;


BMediaGraph::BMediaGraph()  {}
BMediaGraph::~BMediaGraph() {}


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
BMediaGraph::Connect(BMediaOutput* /*output*/, BMediaInput* /*input*/)
{
	return B_OK;
}


status_t
BMediaGraph::Disconnect(BMediaOutput* /*output*/, BMediaInput* /*input*/)
{
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
BMediaGraph::SetDefaultAudioOutput(media_client_id /*id*/)
{
	return B_OK;
}


status_t
BMediaGraph::SetDefaultAudioInput(media_client_id /*id*/)
{
	return B_OK;
}
