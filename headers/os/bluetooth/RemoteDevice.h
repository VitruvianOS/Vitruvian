/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 *
 * Reimplemented on top of BlueZBackend instead of raw HCI.
 * Divergences from haiku-latest/headers/os/bluetooth/RemoteDevice.h:
 *  - Identity is BlueZ's D-Bus object path (BString), not bdaddr_t/HCI
 *    handles -- there is no HCI connection handle when BlueZ owns the link.
 *  - Connect/Disconnect/Pair/Unpair are async (BMessenger reply) instead of
 *    throwing synchronous calls: each is a D-Bus round trip that must not
 *    block a window thread (see BlueZBackend.h's async request/reply note).
 *  - GetFriendlyName() etc. read a locally cached snapshot instead of doing
 *    a live query -- the snapshot is populated by DevicesFromMessage() from
 *    a BlueZBackend::GetDevicesAsync()/GetStatusAsync() reply.
 */
#ifndef _REMOTE_DEVICE_H
#define _REMOTE_DEVICE_H

#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#include <bluetooth/DeviceClass.h>

class BMessage;

namespace Bluetooth {

class RemoteDevice;
typedef BObjectList<RemoteDevice, true> RemoteDevicesList;

class RemoteDevice {

public:
	// Thin wrapper for BlueZBackend::GetDevicesAsync() -- keeps
	// BlueZBackend.h out of consumers entirely.
	static status_t FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat);

	// Populates outList from a BlueZBackend::GetDevices()-shaped BMessage
	// (device_count/device_N). outList owns the RemoteDevice instances it
	// receives (constructed with ownership).
	static status_t DevicesFromMessage(const BMessage& devicesReply,
		RemoteDevicesList& outList);

	explicit RemoteDevice(const BMessage& deviceInfo);
	~RemoteDevice();

	const BString& Path() const { return fPath; }
	const BString& AdapterPath() const { return fAdapterPath; }
	BString GetFriendlyName() const;
	BString GetBluetoothAddress() const { return fAddress; }
	DeviceClass GetDeviceClass() const { return fDeviceClass; }

	bool IsPaired() const { return fPaired; }
	bool IsConnected() const { return fConnected; }
	bool IsTrustedDevice() const { return fTrusted; }
	bool IsBlockedDevice() const { return fBlocked; }

	bool Equals(RemoteDevice* other) const;

	// Async: see the file comment above for why these can't be the
	// throwing synchronous calls upstream has.
	status_t Connect(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Disconnect(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Pair(const BMessenger& replyTo, uint32 replyWhat) const;
	status_t Unpair(const BMessenger& replyTo, uint32 replyWhat) const;

	// org.bluez.Device1 Trusted/Blocked. See BlueZBackend.h's comment on
	// SetDeviceTrusted for how Trusted affects the pairing agent.
	status_t SetTrusted(bool trusted, const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t SetBlocked(bool blocked, const BMessenger& replyTo,
		uint32 replyWhat) const;

private:
	BString fPath;
	BString fAdapterPath;
	BString fAddress;
	BString fName;
	BString fAlias;
	DeviceClass fDeviceClass;
	bool fPaired;
	bool fConnected;
	bool fTrusted;
	bool fBlocked;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::RemoteDevice;
using Bluetooth::RemoteDevicesList;
#endif

#endif // _REMOTE_DEVICE_H
