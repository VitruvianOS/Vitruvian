/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/RemoteDevice.h>

#include <Message.h>

#include "BlueZBackend.h"


namespace Bluetooth {


status_t
RemoteDevice::FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->GetDevicesAsync(replyTo, replyWhat);
}


status_t
RemoteDevice::DevicesFromMessage(const BMessage& devicesReply,
	RemoteDevicesList& outList)
{
	int32 count = 0;
	if (devicesReply.FindInt32("device_count", &count) != B_OK)
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "device_" << i;

		BMessage info;
		if (devicesReply.FindMessage(name.String(), &info) != B_OK)
			continue;

		outList.AddItem(new RemoteDevice(info));
	}

	return B_OK;
}


RemoteDevice::RemoteDevice(const BMessage& deviceInfo)
	:
	fPaired(false),
	fConnected(false),
	fTrusted(false),
	fBlocked(false)
{
	const char* path;
	if (deviceInfo.FindString("path", &path) == B_OK)
		fPath = path;

	const char* adapter;
	if (deviceInfo.FindString("adapter", &adapter) == B_OK)
		fAdapterPath = adapter;

	const char* address;
	if (deviceInfo.FindString("address", &address) == B_OK)
		fAddress = address;

	const char* name;
	if (deviceInfo.FindString("name", &name) == B_OK)
		fName = name;

	const char* alias;
	if (deviceInfo.FindString("alias", &alias) == B_OK)
		fAlias = alias;

	uint32 classRecord;
	if (deviceInfo.FindUInt32("class", &classRecord) == B_OK)
		fDeviceClass.SetRecord(classRecord);

	deviceInfo.FindBool("paired", &fPaired);
	deviceInfo.FindBool("connected", &fConnected);
	deviceInfo.FindBool("trusted", &fTrusted);
	deviceInfo.FindBool("blocked", &fBlocked);
}


RemoteDevice::~RemoteDevice()
{
}


BString
RemoteDevice::GetFriendlyName() const
{
	if (!fAlias.IsEmpty())
		return fAlias;
	if (!fName.IsEmpty())
		return fName;
	return fAddress;
}


bool
RemoteDevice::Equals(RemoteDevice* other) const
{
	return other != NULL && fPath == other->fPath;
}


status_t
RemoteDevice::Connect(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->ConnectDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Disconnect(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->DisconnectDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Pair(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->PairDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::Unpair(const BMessenger& replyTo, uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->UnpairDeviceAsync(fPath.String(), replyTo, replyWhat);
}


status_t
RemoteDevice::SetTrusted(bool trusted, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetDeviceTrustedAsync(fPath.String(), trusted, replyTo,
		replyWhat);
}


status_t
RemoteDevice::SetBlocked(bool blocked, const BMessenger& replyTo,
	uint32 replyWhat) const
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->SetDeviceBlockedAsync(fPath.String(), blocked, replyTo,
		replyWhat);
}


} // namespace Bluetooth
