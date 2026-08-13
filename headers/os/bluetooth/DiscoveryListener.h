/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 *
 * Reimplemented on top of BlueZBackend. Kept the shape upstream
 * already had -- BLooper-based, listener owns its own thread -- because that
 * is exactly what makes a listener-based API fit an async D-Bus backend
 * without a synchronous rendezvous. See DiscoveryListener.cpp for which
 * thread DeviceDiscovered()/InquiryStarted()/InquiryCompleted() run on.
 */
#ifndef _DISCOVERY_LISTENER_H
#define _DISCOVERY_LISTENER_H

#include <Looper.h>
#include <ObjectList.h>

#include <bluetooth/DeviceClass.h>
#include <bluetooth/RemoteDevice.h>


#define BT_INQUIRY_COMPLETED	0x01
#define BT_INQUIRY_TERMINATED	0x02
#define BT_INQUIRY_ERROR		0x03


namespace Bluetooth {

class LocalDevice;
class DiscoveryAgent;

class DiscoveryListener : public BLooper {

public:
	static const int INQUIRY_COMPLETED = BT_INQUIRY_COMPLETED;
	static const int INQUIRY_TERMINATED = BT_INQUIRY_TERMINATED;
	static const int INQUIRY_ERROR = BT_INQUIRY_ERROR;

	DiscoveryListener();

	virtual void DeviceDiscovered(RemoteDevice* device, DeviceClass cod);
	virtual void InquiryStarted(status_t status);
	virtual void InquiryCompleted(int discType);

private:
	virtual void MessageReceived(BMessage* message);

	// Private protocol between DiscoveryAgent and its listener; BlueZBackend's
	// own NOTIFICATION_DEVICE_FOUND is handled directly (the agent points
	// BlueZBackend's watcher straight at the listener, see DiscoveryAgent.cpp).
	static const uint32 kInquiryStartAckWhat = 'btSA';
	static const uint32 kInquiryTimeoutWhat = 'btIT';
	static const uint32 kInquiryTerminatedWhat = 'btIX';

	RemoteDevicesList fRemoteDevicesList;

	friend class DiscoveryAgent;
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::DiscoveryListener;
#endif

#endif // _DISCOVERY_LISTENER_H
