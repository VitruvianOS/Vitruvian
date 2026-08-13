/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/LocalDevice.h>
#include <bluetooth/DiscoveryAgent.h>

#include <Message.h>

#include "BlueZBackend.h"


namespace Bluetooth {


status_t
LocalDevice::StartWatching(const BMessenger& target, uint32 notificationMask)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StartWatching(target, notificationMask);
}


status_t
LocalDevice::StopWatching(const BMessenger& target)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StopWatching(target);
}


status_t
LocalDevice::RegisterAgent(const BMessenger& uiHandler,
	const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->RegisterAgentAsync(uiHandler, replyTo, replyWhat);
}


status_t
LocalDevice::UnregisterAgent(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->UnregisterAgentAsync(replyTo, replyWhat);
}


void
LocalDevice::CompleteAgentRequest(uint32 requestId, bool accepted,
	const BString& value)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return;

	backend->CompleteAgentRequest(requestId, accepted, value);
}


status_t
LocalDevice::FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetAdaptersAsync(replyTo, replyWhat);
}


status_t
LocalDevice::FetchStatusAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetStatusAsync(replyTo, replyWhat);
}


status_t
LocalDevice::DevicesFromMessage(const BMessage& adaptersReply,
	LocalDevicesList& outList)
{
	int32 count = 0;
	if (adaptersReply.FindInt32("adapter_count", &count) != B_OK)
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "adapter_" << i;

		BMessage info;
		if (adaptersReply.FindMessage(name.String(), &info) != B_OK)
			continue;

		outList.AddItem(new LocalDevice(info));
	}

	return B_OK;
}


LocalDevice::LocalDevice(const BMessage& adapterInfo)
	:
	fPowered(false),
	fDiscoverable(false),
	fDiscovering(false),
	fPairable(false)
{
	const char* path;
	if (adapterInfo.FindString("path", &path) == B_OK)
		fPath = path;

	const char* address;
	if (adapterInfo.FindString("address", &address) == B_OK)
		fAddress = address;

	const char* name;
	if (adapterInfo.FindString("alias", &name) == B_OK)
		fName = name;
	else if (adapterInfo.FindString("name", &name) == B_OK)
		fName = name;

	uint32 classRecord;
	if (adapterInfo.FindUInt32("class", &classRecord) == B_OK)
		fDeviceClass.SetRecord(classRecord);

	adapterInfo.FindBool("powered", &fPowered);
	adapterInfo.FindBool("discoverable", &fDiscoverable);
	adapterInfo.FindBool("discovering", &fDiscovering);
	adapterInfo.FindBool("pairable", &fPairable);
}


LocalDevice::~LocalDevice()
{
}


status_t
LocalDevice::SetPowered(bool powered, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterPoweredAsync(fPath.String(), powered, replyTo,
		replyWhat);
}


status_t
LocalDevice::SetDiscoverable(bool discoverable, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterDiscoverableAsync(fPath.String(), discoverable,
		replyTo, replyWhat);
}


status_t
LocalDevice::SetPairable(bool pairable, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetAdapterPairableAsync(fPath.String(), pairable, replyTo,
		replyWhat);
}


status_t
LocalDevice::StartDiscovery(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StartDiscoveryAsync(fPath.String(), replyTo, replyWhat);
}


status_t
LocalDevice::StopDiscovery(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StopDiscoveryAsync(fPath.String(), replyTo, replyWhat);
}


DiscoveryAgent*
LocalDevice::GetDiscoveryAgent() const
{
	return new DiscoveryAgent(fPath);
}


} // namespace Bluetooth
