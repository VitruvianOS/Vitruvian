/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NMBackend.h"
#include <NetworkInterface.h>
#include <NetworkAddress.h>
#include <Message.h>
#include <net/if_types.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h>


// Resolves and caches fDevicePath for fName; NMBackend's calls are keyed on
// the D-Bus object path, not the interface name. Returns NULL if there is
// no backend or NM doesn't know this interface.
static NMBackend*
_ResolvedBackend(const char* name, BString& devicePath)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return NULL;

	if (devicePath.Length() == 0
		&& backend->_ResolveDevicePath(name, devicePath) != B_OK) {
		return NULL;
	}

	return backend;
}


// Shared by GetAddressAt() and BNetworkInterfaceAddress::SetTo(): pull live address `index` of the
// named interface from NMBackend's snapshot. Literals are parsed with inet_pton, never the resolver.
static status_t
_GetInterfaceAddress(const char* name, int32 index,
	BNetworkInterfaceAddress& address)
{
	BString devicePath;
	NMBackend* backend = _ResolvedBackend(name, devicePath);
	if (backend == NULL)
		return B_ERROR;

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(devicePath.String(), &deviceInfo) != B_OK)
		return B_ERROR;

	int32 count = 0;
	if (deviceInfo.FindInt32(kNMFieldAddressCount, &count) != B_OK
		|| index < 0 || index >= count) {
		return B_ENTRY_NOT_FOUND;
	}

	char key[32];
	snprintf(key, sizeof(key), "address_%" B_PRId32, index);
	BMessage entry;
	if (deviceInfo.FindMessage(key, &entry) != B_OK)
		return B_ENTRY_NOT_FOUND;

	BString addressString;
	int32 family = AF_INET;
	int32 prefix = 0;
	if (entry.FindString(kNMFieldAddressString, &addressString) != B_OK
		|| entry.FindInt32(kNMFieldAddressFamily, &family) != B_OK) {
		return B_ERROR;
	}
	entry.FindInt32(kNMFieldAddressPrefix, &prefix);

	if (family == AF_INET) {
		struct in_addr inetAddress;
		if (inet_pton(AF_INET, addressString.String(), &inetAddress) != 1)
			return B_BAD_VALUE;
		address.Address().SetTo(inetAddress.s_addr, 0);
	} else if (family == AF_INET6) {
		struct in6_addr inetAddress;
		if (inet_pton(AF_INET6, addressString.String(), &inetAddress) != 1)
			return B_BAD_VALUE;
		address.Address().SetTo(inetAddress, 0);
	} else {
		return B_NOT_SUPPORTED;
	}

	if (prefix > 0)
		address.Mask().SetToMask(family, (uint32)prefix);

	address.SetFlags(0);
	return B_OK;
}


BNetworkInterfaceAddress::BNetworkInterfaceAddress()
	:
	fIndex(-1),
	fFlags(0)
{
}


BNetworkInterfaceAddress::~BNetworkInterfaceAddress()
{
}


status_t
BNetworkInterfaceAddress::SetTo(const BNetworkInterface& interface, int32 index)
{
	// GetAddressAt() is non-const, but this method only needs the interface name, as the kit's public lookup does.
	status_t status = _GetInterfaceAddress(interface.Name(), index, *this);
	if (status == B_OK)
		fIndex = index;
	return status;
}


void
BNetworkInterfaceAddress::SetAddress(const BNetworkAddress& address)
{
	fAddress = address;
}


void
BNetworkInterfaceAddress::SetMask(const BNetworkAddress& mask)
{
	fMask = mask;
}


void
BNetworkInterfaceAddress::SetBroadcast(const BNetworkAddress& broadcast)
{
	fBroadcast = broadcast;
}


void
BNetworkInterfaceAddress::SetDestination(const BNetworkAddress& destination)
{
	fBroadcast = destination;
}


void
BNetworkInterfaceAddress::SetFlags(uint32 flags)
{
	fFlags = flags;
}


// #pragma mark -


BNetworkInterface::BNetworkInterface()
{
	Unset();
}


BNetworkInterface::BNetworkInterface(const char* name)
{
	SetTo(name);
}


BNetworkInterface::BNetworkInterface(uint32 index)
{
	SetTo(index);
}


BNetworkInterface::~BNetworkInterface()
{
}


void
BNetworkInterface::Unset()
{
	fName[0] = '\0';
	fDevicePath.Truncate(0);
}


void
BNetworkInterface::SetTo(const char* name)
{
	strlcpy(fName, name, IF_NAMESIZE);
	fDevicePath.Truncate(0);
}


status_t
BNetworkInterface::SetTo(uint32 index)
{
	// TODO: Query NetworkManager to get interface name from index
	strlcpy(fName, "", IF_NAMESIZE);
	return B_OK;
}


bool
BNetworkInterface::Exists() const
{
	NMBackend* backend = _ResolvedBackend(fName, fDevicePath);
	if (backend == NULL)
		return false;

	BMessage deviceInfo;
	return backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) == B_OK;
}


const char*
BNetworkInterface::Name() const
{
	return fName;
}


uint32
BNetworkInterface::Index() const
{
	// TODO: Query NetworkManager for interface index
	return 0;
}


uint32
BNetworkInterface::Flags() const
{
	// NMBackend's GetDeviceInfo() has no "flags" field -- IFF_* flags are a
	// kernel/glibc concept NM doesn't model, so read them straight from
	// getifaddrs() the way BNetworkDevice::Flags() does.
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


uint32
BNetworkInterface::MTU() const
{
	NMBackend* backend = _ResolvedBackend(fName, fDevicePath);
	if (backend == NULL)
		return 1500; // Default MTU

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) == B_OK) {
		uint32 mtu;
		if (deviceInfo.FindUInt32(kNMFieldMTU, &mtu) == B_OK)
			return mtu;
	}

	return 1500; // Default MTU
}


int32
BNetworkInterface::Media() const
{
	// TODO: Query NetworkManager for media type
	return 0;
}


uint32
BNetworkInterface::Metric() const
{
	// NetworkManager doesn't use per-interface metrics in the same way
	return 0;
}


uint32
BNetworkInterface::Type() const
{
	NMBackend* backend = _ResolvedBackend(fName, fDevicePath);
	if (backend == NULL)
		return 0;

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) == B_OK) {
		const char* deviceType;
		if (deviceInfo.FindString(kNMFieldType, &deviceType) == B_OK) {
			// Map NetworkManager device types to Haiku types
			if (strcmp(deviceType, "ethernet") == 0)
				return IFT_ETHER;
			else if (strcmp(deviceType, "wifi") == 0)
				return IFT_IEEE80211;
			else if (strcmp(deviceType, "bluetooth") == 0)
				return IFT_BLUETOOTH;
		}
	}
	
	return 0;
}


static bool
read_sysfs_stat(const char* ifName, const char* stat, uint64& value)
{
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s",
		ifName, stat);

	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	unsigned long long parsed = 0;
	bool ok = fscanf(file, "%llu", &parsed) == 1;
	fclose(file);

	value = parsed;
	return ok;
}


status_t
BNetworkInterface::GetStats(ifreq_stats& stats)
{
	// Plain kernel counters -- read straight from sysfs instead of going
	// through NMBackend/libnm: this has nothing to do with connection
	// management, and would otherwise mean a D-Bus round trip per poll.
	memset(&stats, 0, sizeof(stats));

	uint64 value;
	bool any = false;
	if (read_sysfs_stat(fName, "rx_bytes", value)) {
		stats.receive.bytes = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "tx_bytes", value)) {
		stats.send.bytes = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "rx_packets", value)) {
		stats.receive.packets = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "tx_packets", value)) {
		stats.send.packets = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "rx_errors", value)) {
		stats.receive.errors = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "tx_errors", value)) {
		stats.send.errors = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "rx_dropped", value)) {
		stats.receive.dropped = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "tx_dropped", value)) {
		stats.send.dropped = value;
		any = true;
	}
	if (read_sysfs_stat(fName, "collisions", value)) {
		stats.collisions = value;
		any = true;
	}

	return any ? B_OK : B_ERROR;
}


bool
BNetworkInterface::HasLink() const
{
	uint32 flags = Flags();
	return (flags & IFF_UP) != 0 && (flags & IFF_RUNNING) != 0;
}


status_t
BNetworkInterface::GetHardwareAddress(BNetworkAddress& address)
{
	NMBackend* backend = _ResolvedBackend(fName, fDevicePath);
	if (backend == NULL)
		return B_ERROR;

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(fDevicePath.String(), &deviceInfo) != B_OK)
		return B_ERROR;

	const char* macAddress;
	if (deviceInfo.FindString(kNMFieldHWAddress, &macAddress) != B_OK)
		return B_ERROR;

	// Parse "aa:bb:cc:dd:ee:ff" into raw bytes for SetToLinkLevel().
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


int32
BNetworkInterface::CountAddresses() const
{
	BString devicePath(fDevicePath);
	NMBackend* backend = _ResolvedBackend(fName, devicePath);
	if (backend == NULL)
		return 0;

	BMessage deviceInfo;
	if (backend->GetDeviceInfo(devicePath.String(), &deviceInfo) != B_OK)
		return 0;

	int32 count = 0;
	deviceInfo.FindInt32(kNMFieldAddressCount, &count);
	return count;
}


status_t
BNetworkInterface::GetAddressAt(int32 index, BNetworkInterfaceAddress& address)
{
	return _GetInterfaceAddress(fName, index, address);
}


int32
BNetworkInterface::FindFirstAddress(int family)
{
	int32 count = CountAddresses();
	for (int32 i = 0; i < count; i++) {
		BNetworkInterfaceAddress address;
		if (GetAddressAt(i, address) == B_OK
			&& address.Address().Family() == family) {
			return i;
		}
	}
	return -1;
}


status_t
BNetworkInterface::AddAddress(const BNetworkInterfaceAddress& address)
{
	// TODO: Use NetworkManager to add address
	return B_OK;
}


status_t
BNetworkInterface::RemoveAddress(const BNetworkInterfaceAddress& address)
{
	// TODO: Use NetworkManager to remove address
	return B_OK;
}


status_t
BNetworkInterface::SetFlags(uint32 flags)
{
	// TODO: Use NetworkManager to set flags
	return B_OK;
}


status_t
BNetworkInterface::SetMTU(uint32 mtu)
{
	// TODO: Use NetworkManager to set MTU
	return B_OK;
}


status_t
BNetworkInterface::SetMetric(uint32 metric)
{
	// NetworkManager doesn't use per-interface metrics
	return B_OK;
}