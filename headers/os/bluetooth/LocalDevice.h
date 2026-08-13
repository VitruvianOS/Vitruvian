/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 *
 * Reimplemented on top of BlueZBackend instead of raw HCI.
 * Divergences from haiku-latest/headers/os/bluetooth/LocalDevice.h:
 *  - Identity is BlueZ's adapter D-Bus object path (BString), not hci_id/
 *    bdaddr_t -- BlueZ never exposes a raw HCI handle over D-Bus.
 *  - No throwing GetLocalDevice()/GetLocalDeviceCount() singleton lookup:
 *    adapter enumeration is an async D-Bus round trip (FetchAllAsync()); the
 *    reply is handed back to the caller to parse (DevicesFromMessage())
 *    instead of the kit holding a hidden synchronous cache.
 *  - SetPowered()/SetDiscoverable()/StartDiscovery()/StopDiscovery() are
 *    async (BMessenger reply) rather than throwing synchronous calls -- see
 *    BlueZBackend.h's async request/reply note; this is the single worst
 *    recurring defect class in this project and the kit must not reintroduce
 *    a synchronous rendezvous on a window thread.
 */
#ifndef _LOCAL_DEVICE_H
#define _LOCAL_DEVICE_H

#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#include <bluetooth/DeviceClass.h>

class BMessage;

namespace Bluetooth {

class DiscoveryAgent;

class LocalDevice;
typedef BObjectList<LocalDevice, true> LocalDevicesList;

class LocalDevice {

public:
	// Mirrors BlueZBackend::NotificationType exactly -- these are the literal
	// `what` codes BlueZBackend posts to a watching BMessenger, so the
	// values must match; this is a pass-through, not a translation layer.
	enum NotificationType {
		NOTIFICATION_ADAPTER_ADDED = 'BTAD',
		NOTIFICATION_ADAPTER_REMOVED = 'BTRM',
		NOTIFICATION_ADAPTER_PROPERTY_CHANGED = 'BTAP',
		NOTIFICATION_DEVICE_FOUND = 'BTDF',
		NOTIFICATION_DEVICE_CONNECTED = 'BTDC',
		NOTIFICATION_DEVICE_DISCONNECTED = 'BTDD',
		NOTIFICATION_DEVICE_PROPERTY_CHANGED = 'BTDP'
	};

	// Thin wrappers for BlueZBackend's watcher registration and adapter/
	// device enumeration -- keep BlueZBackend.h out of consumers entirely.
	static status_t StartWatching(const BMessenger& target,
		uint32 notificationMask);
	static status_t StopWatching(const BMessenger& target);

	// org.bluez.Agent1, capability KeyboardDisplay -- see BlueZBackend.h for
	// the full contract. uiHandler receives kAgentRequest ("request_id",
	// "kind" a pairing_dialog_kind value, "device_name", per-kind fields)
	// and kAgentCancel; both calls return immediately, safe from a window
	// thread. Register/unregister is idempotent and reentry-safe so it can
	// be driven straight from AttachedToWindow()/DetachedFromWindow().
	enum {
		kAgentRequest = 'BTAR',
		kAgentCancel = 'BTAC'
	};

	static status_t RegisterAgent(const BMessenger& uiHandler,
		const BMessenger& replyTo, uint32 replyWhat);
	static status_t UnregisterAgent(const BMessenger& replyTo,
		uint32 replyWhat);
	static void CompleteAgentRequest(uint32 requestId, bool accepted,
		const BString& value);

	static status_t FetchAllAsync(const BMessenger& replyTo, uint32 replyWhat);

	// Combined adapters+devices snapshot in one round trip; reply carries
	// "adapters" and "devices" sub-BMessages, each in DevicesFromMessage()/
	// RemoteDevice::DevicesFromMessage() shape.
	static status_t FetchStatusAsync(const BMessenger& replyTo,
		uint32 replyWhat);

	// Populates outList from the resulting BMessage (adapter_count/adapter_N
	// shape). outList owns the LocalDevice instances it receives.
	static status_t DevicesFromMessage(const BMessage& adaptersReply,
		LocalDevicesList& outList);

	explicit LocalDevice(const BMessage& adapterInfo);
	~LocalDevice();

	const BString& Path() const { return fPath; }
	BString GetFriendlyName() const { return fName; }
	BString GetBluetoothAddress() const { return fAddress; }
	DeviceClass GetDeviceClass() const { return fDeviceClass; }
	bool IsPowered() const { return fPowered; }
	bool IsDiscoverable() const { return fDiscoverable; }
	bool IsDiscovering() const { return fDiscovering; }
	// org.bluez.Adapter1.Pairable: honest mapping for "incoming connections
	// policy" -- BlueZ has no multi-level policy, only this on/off gate on
	// whether the adapter accepts incoming pairing requests at all.
	bool IsPairable() const { return fPairable; }

	status_t SetPowered(bool powered, const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t SetDiscoverable(bool discoverable, const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t SetPairable(bool pairable, const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t StartDiscovery(const BMessenger& replyTo,
		uint32 replyWhat) const;
	status_t StopDiscovery(const BMessenger& replyTo, uint32 replyWhat) const;

	// Caller owns the returned agent.
	DiscoveryAgent* GetDiscoveryAgent() const;

private:
	BString fPath;
	BString fAddress;
	BString fName;
	DeviceClass fDeviceClass;
	bool fPowered;
	bool fDiscoverable;
	bool fDiscovering;
	bool fPairable;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::LocalDevice;
using Bluetooth::LocalDevicesList;
#endif

#endif // _LOCAL_DEVICE_H
