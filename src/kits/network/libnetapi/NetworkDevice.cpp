/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <NetworkDevice.h>

#include <net/if.h>
#include <string.h>

#include <ifaddrs.h>

#include "NMBackend.h"


BNetworkDevice::BNetworkDevice()
{
	Unset();
}


BNetworkDevice::BNetworkDevice(const char* name)
{
	SetTo(name);
}


BNetworkDevice::~BNetworkDevice()
{
}


void
BNetworkDevice::Unset()
{
	fName[0] = '\0';
	fDevicePath.Truncate(0);
}


void
BNetworkDevice::SetTo(const char* name)
{
	if (name == NULL) {
		Unset();
		return;
	}

	strncpy(fName, name, IF_NAMESIZE - 1);
	fName[IF_NAMESIZE - 1] = '\0';

	fDevicePath.Truncate(0);
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL)
		backend->_ResolveDevicePath(fName, fDevicePath);
}


const char*
BNetworkDevice::Name() const
{
	return fName;
}


bool
BNetworkDevice::Exists() const
{
	return if_nametoindex(fName) != 0;
}


uint32
BNetworkDevice::Index() const
{
	return if_nametoindex(fName);
}


uint32
BNetworkDevice::Flags() const
{
	uint32 flags = 0;
	struct ifaddrs* addrs;
	if (getifaddrs(&addrs) != 0)
		return 0;

	for (struct ifaddrs* addr = addrs; addr != NULL; addr = addr->ifa_next) {
		if (strcmp(addr->ifa_name, fName) == 0) {
			flags = addr->ifa_flags;
			break;
		}
	}

	freeifaddrs(addrs);
	return flags;
}


bool
BNetworkDevice::HasLink() const
{
	uint32 flags = Flags();
	return (flags & IFF_UP) != 0 && (flags & IFF_RUNNING) != 0;
}


int32
BNetworkDevice::Media() const
{
	// ifmedia is BSD-only; NetworkManager doesn't expose an equivalent.
	return 0;
}


status_t
BNetworkDevice::SetMedia(int32 media)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkDevice::GetHardwareAddress(BNetworkAddress& address)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (fDevicePath.Length() == 0
		&& backend->_ResolveDevicePath(fName, fDevicePath) != B_OK) {
		return B_ERROR;
	}

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) != B_OK)
		return B_ERROR;

	const char* macAddress;
	if (deviceInfo.FindString(kNMFieldHWAddress, &macAddress) != B_OK)
		return B_ERROR;

	uint8 bytes[6];
	unsigned int parsed[6];
	if (sscanf(macAddress, "%x:%x:%x:%x:%x:%x", &parsed[0], &parsed[1],
			&parsed[2], &parsed[3], &parsed[4], &parsed[5]) != 6) {
		return B_ERROR;
	}
	for (int i = 0; i < 6; i++)
		bytes[i] = (uint8)parsed[i];

	address.SetToLinkLevel(bytes, sizeof(bytes));
	return B_OK;
}


bool
BNetworkDevice::IsEthernet()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return false;

	if (fDevicePath.Length() == 0
		&& backend->_ResolveDevicePath(fName, fDevicePath) != B_OK) {
		return false;
	}

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) != B_OK)
		return false;

	const char* type;
	return deviceInfo.FindString(kNMFieldType, &type) == B_OK
		&& strcmp(type, "ethernet") == 0;
}


bool
BNetworkDevice::IsWireless()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return false;

	if (fDevicePath.Length() == 0
		&& backend->_ResolveDevicePath(fName, fDevicePath) != B_OK) {
		return false;
	}

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) != B_OK)
		return false;

	const char* type;
	return deviceInfo.FindString(kNMFieldType, &type) == B_OK
		&& strcmp(type, "wifi") == 0;
}


status_t
BNetworkDevice::Control(int option, void* request)
{
	// Raw ioctl passthrough doesn't have an NM equivalent.
	return B_NOT_SUPPORTED;
}


status_t
BNetworkDevice::Scan(bool wait, bool forceRescan)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (fDevicePath.Length() == 0
		&& backend->_ResolveDevicePath(fName, fDevicePath) != B_OK) {
		return B_ERROR;
	}

	BMessage networks;
	return backend->ScanWiFiNetworks(fDevicePath.String(), &networks);
}


status_t
BNetworkDevice::GetNetworks(wireless_network*& networks, uint32& count)
{
	// TODO: translate NMBackend::ScanWiFiNetworks()'s BMessage result into
	// wireless_network entries once the wifi scan path is fleshed out.
	networks = NULL;
	count = 0;
	return B_OK;
}


status_t
BNetworkDevice::GetNetwork(const char* name, wireless_network& network)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
BNetworkDevice::GetNetwork(const BNetworkAddress& address,
	wireless_network& network)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
BNetworkDevice::JoinNetwork(const char* name, const char* password)
{
	if (name == NULL)
		return B_BAD_VALUE;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	if (fDevicePath.Length() == 0
		&& backend->_ResolveDevicePath(fName, fDevicePath) != B_OK) {
		return B_ERROR;
	}

	return backend->ConnectToWiFi(fDevicePath.String(), name, password, NULL);
}


status_t
BNetworkDevice::JoinNetwork(const wireless_network& network,
	const char* password)
{
	return JoinNetwork(network.name, password);
}


status_t
BNetworkDevice::JoinNetwork(const BNetworkAddress& address,
	const char* password)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkDevice::LeaveNetwork(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return B_ERROR;

	return backend->ForgetWiFiNetwork(name);
}


status_t
BNetworkDevice::LeaveNetwork(const wireless_network& network)
{
	return LeaveNetwork(network.name);
}


status_t
BNetworkDevice::LeaveNetwork(const BNetworkAddress& address)
{
	return B_NOT_SUPPORTED;
}


status_t
BNetworkDevice::GetNextAssociatedNetwork(uint32& cookie,
	wireless_network& network)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
BNetworkDevice::GetNextAssociatedNetwork(uint32& cookie,
	BNetworkAddress& address)
{
	return B_ENTRY_NOT_FOUND;
}
