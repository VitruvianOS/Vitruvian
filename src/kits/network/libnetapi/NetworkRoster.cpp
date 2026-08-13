/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NMBackend.h"
#include <NetworkRoster.h>
#include <NetworkInterface.h>
#include <Message.h>

#include <stdio.h>
#include <string.h>


BNetworkRoster BNetworkRoster::sDefault;


BNetworkRoster::BNetworkRoster()
{
}


BNetworkRoster::~BNetworkRoster()
{
}


BNetworkRoster&
BNetworkRoster::Default()
{
	return sDefault;
}


size_t
BNetworkRoster::CountInterfaces() const
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return 0;

	BMessage devices;
	if (backend->GetDevices(&devices) != B_OK)
		return 0;

	int32 deviceCount = 0;
	devices.FindInt32("device_count", &deviceCount);
	return (size_t)deviceCount;
}


status_t
BNetworkRoster::GetNextInterface(uint32* cookie, BNetworkInterface& interface) const
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (cookie == NULL)
		return B_BAD_VALUE;

	BMessage devices;
	status_t result = backend->GetDevices(&devices);
	if (result != B_OK)
		return result;

	int32 deviceCount = 0;
	if (devices.FindInt32("device_count", &deviceCount) != B_OK)
		return B_ERROR;

	if (*cookie >= (uint32)deviceCount)
		return B_ENTRY_NOT_FOUND;

	// Get device at current cookie index
	BMessage deviceInfo;
	char deviceName[32];
	snprintf(deviceName, sizeof(deviceName), "device_%" B_PRIu32, *cookie);

	if (devices.FindMessage(deviceName, &deviceInfo) != B_OK)
		return B_ERROR;

	// NMBackend::GetDevices() stores the kernel interface name under
	// kNMFieldInterface ("name" is never added).
	const char* interfaceName;
	if (deviceInfo.FindString(kNMFieldInterface, &interfaceName) != B_OK)
		return B_ERROR;

	interface.SetTo(interfaceName);
	(*cookie)++;

	return B_OK;
}


status_t
BNetworkRoster::AddInterface(const char* name)
{
	// Creating virtual interfaces isn't exposed by NetworkManager's D-Bus
	// API in a way NMBackend wraps yet.
	return B_NOT_SUPPORTED;
}


status_t
BNetworkRoster::AddInterface(const BNetworkInterface& interface)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkRoster::RemoveInterface(const char* name)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkRoster::RemoveInterface(const BNetworkInterface& interface)
{
	return B_NOT_SUPPORTED;
}


int32
BNetworkRoster::CountPersistentNetworks() const
{
	// Persistent (saved) wireless network profiles aren't yet exposed
	// through NMBackend.
	return 0;
}


status_t
BNetworkRoster::GetNextPersistentNetwork(uint32* cookie,
	wireless_network& network) const
{
	return B_ENTRY_NOT_FOUND;
}


status_t
BNetworkRoster::AddPersistentNetwork(const wireless_network& network)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkRoster::RemovePersistentNetwork(const char* name)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkRoster::StartWatching(const BMessenger& target, uint32 eventMask)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->StartWatching(target, eventMask);
}


void
BNetworkRoster::StopWatching(const BMessenger& target)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	backend->StopWatching(target);
}
