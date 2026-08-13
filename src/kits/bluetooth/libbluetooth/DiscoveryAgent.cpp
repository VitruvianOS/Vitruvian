/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/DiscoveryAgent.h>

#include <MessageRunner.h>
#include <Messenger.h>

#include "BlueZBackend.h"


namespace Bluetooth {


DiscoveryAgent::DiscoveryAgent(const BString& adapterPath)
	:
	fAdapterPath(adapterPath),
	fListener(NULL),
	fTimeoutRunner(NULL)
{
}


DiscoveryAgent::~DiscoveryAgent()
{
	if (fListener != NULL)
		CancelInquiry(fListener);
}


status_t
DiscoveryAgent::StartInquiry(DiscoveryListener* listener, bigtime_t secs)
{
	if (listener == NULL || fAdapterPath.IsEmpty())
		return B_BAD_VALUE;

	if (secs < BT_MIN_INQUIRY_TIME_SECS * 1000000LL)
		secs = BT_MIN_INQUIRY_TIME_SECS * 1000000LL;
	else if (secs > BT_MAX_INQUIRY_TIME_SECS * 1000000LL)
		secs = BT_MAX_INQUIRY_TIME_SECS * 1000000LL;

	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (fListener != NULL)
		CancelInquiry(fListener);

	fListener = listener;

	// Claims BlueZBackend's single watcher slot for the scan's duration --
	// see the class comment in DiscoveryAgent.h.
	backend->StartWatching(BMessenger(listener),
		BlueZBackend::NOTIFICATION_DEVICE_FOUND);

	backend->StartDiscoveryAsync(fAdapterPath.String(), BMessenger(listener),
		DiscoveryListener::kInquiryStartAckWhat);

	// Unlike real HCI hardware, BlueZ's StartDiscovery keeps scanning until
	// explicitly stopped -- the timeout message carries the adapter path so
	// DiscoveryListener can stop it before reporting INQUIRY_COMPLETED.
	BMessage* timeoutMessage = new BMessage(DiscoveryListener::kInquiryTimeoutWhat);
	timeoutMessage->AddString("adapter_path", fAdapterPath);

	delete fTimeoutRunner;
	fTimeoutRunner = new BMessageRunner(BMessenger(listener), timeoutMessage,
		secs, 1);

	return B_OK;
}


status_t
DiscoveryAgent::CancelInquiry(DiscoveryListener* listener)
{
	if (listener == NULL || fAdapterPath.IsEmpty())
		return B_BAD_VALUE;

	delete fTimeoutRunner;
	fTimeoutRunner = NULL;

	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	backend->StopDiscoveryAsync(fAdapterPath.String(), BMessenger(listener),
		DiscoveryListener::kInquiryTerminatedWhat);
	backend->StopWatching(BMessenger(listener));

	if (fListener == listener)
		fListener = NULL;

	return B_OK;
}


} // namespace Bluetooth
