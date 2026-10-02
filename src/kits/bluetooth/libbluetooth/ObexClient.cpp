/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <ObexClient.h>

#include "BlueZBackend.h"


status_t
ObexClient::RegisterAgent(const BMessenger& uiHandler,
	const BMessenger& replyTo, uint32 replyWhat)
{
	return BlueZBackend::Instance()->RegisterObexAgentAsync(uiHandler,
		replyTo, replyWhat);
}


status_t
ObexClient::UnregisterAgent(const BMessenger& replyTo, uint32 replyWhat)
{
	return BlueZBackend::Instance()->UnregisterObexAgentAsync(replyTo,
		replyWhat);
}


void
ObexClient::CompleteRequest(uint32 requestId, bool accepted)
{
	BlueZBackend::Instance()->CompleteObexRequest(requestId, accepted);
}


status_t
ObexClient::SendFiles(const char* targetAddress, const BMessage& files,
	const BMessenger& uiHandler, const BMessenger& replyTo, uint32 replyWhat)
{
	return BlueZBackend::Instance()->ObexSendFilesAsync(targetAddress, files,
		uiHandler, replyTo, replyWhat);
}


void
ObexClient::CancelSend()
{
	BlueZBackend::Instance()->ObexCancelSend();
}
