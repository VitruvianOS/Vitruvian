/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <bluetooth/DiscoveryListener.h>

#include <Message.h>

#include "BlueZBackend.h"


namespace Bluetooth {


/* hooks -- default no-op, override in a subclass */

void
DiscoveryListener::DeviceDiscovered(RemoteDevice* device, DeviceClass cod)
{
}


void
DiscoveryListener::InquiryStarted(status_t status)
{
}


void
DiscoveryListener::InquiryCompleted(int discType)
{
}


// DiscoveryListener is a BLooper, and Run() here (as upstream does) gives it
// its own thread. DiscoveryAgent talks to it purely via BMessenger::SendMessage,
// so DeviceDiscovered()/InquiryStarted()/InquiryCompleted() below always run
// on *this listener's own thread* -- never the caller's, and never
// BlueZBackend's GMainContext dispatch thread. A BLooper/BHandler subclass
// (e.g. a preflet's device-list view, embedded or targeted via BMessenger)
// can safely touch its own state from these hooks by the normal Handler/
// Looper locking rules; anything reaching into another window's BView must
// still go through that window's BMessenger, as anywhere else in the tree.
DiscoveryListener::DiscoveryListener()
	:
	BLooper("bluetooth discovery listener"),
	fRemoteDevicesList(20)
{
	Run();
}


// BlueZ only announces a device with InterfacesAdded the first time it sees
// it. One it already knows (from an earlier scan, by us or bluetoothctl)
// never reaches the watcher again, so report the unpaired ones from the
// backend's cache too, skipping any already reported.
void
DiscoveryListener::_ReportKnownDevices()
{
	BlueZBackend* backend = BlueZBackend::Instance();
	if (backend == NULL)
		return;

	BMessage devices;
	if (backend->GetDevices(&devices) != B_OK)
		return;

	int32 count = 0;
	devices.FindInt32("device_count", &count);
	for (int32 i = 0; i < count; i++) {
		BString itemName;
		itemName << "device_" << i;
		BMessage info;
		if (devices.FindMessage(itemName.String(), &info) != B_OK)
			continue;

		bool paired = false;
		info.FindBool("paired", &paired);
		if (paired)
			continue;

		BString path;
		info.FindString("path", &path);
		bool alreadyReported = false;
		for (int32 j = 0; j < fRemoteDevicesList.CountItems(); j++) {
			if (path == fRemoteDevicesList.ItemAt(j)->Path()) {
				alreadyReported = true;
				break;
			}
		}
		if (alreadyReported)
			continue;

		RemoteDevice* device = new RemoteDevice(info);
		fRemoteDevicesList.AddItem(device);
		DeviceDiscovered(device, device->GetDeviceClass());
	}
}


void
DiscoveryListener::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case BlueZBackend::NOTIFICATION_DEVICE_FOUND:
		{
			RemoteDevice* device = new RemoteDevice(*message);
			fRemoteDevicesList.AddItem(device);
			DeviceDiscovered(device, device->GetDeviceClass());
			break;
		}

		case kInquiryStartAckWhat:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			fRemoteDevicesList.MakeEmpty();
			InquiryStarted(status);
			if (status == B_OK)
				_ReportKnownDevices();
			break;
		}

		case kInquiryTimeoutWhat:
		{
			// BlueZ, unlike real HCI hardware, keeps scanning until told to
			// stop -- unwind the watcher/discovery state DiscoveryAgent set
			// up before telling the subclass the inquiry is done.
			const char* adapterPath;
			if (message->FindString("adapter_path", &adapterPath) == B_OK) {
				BlueZBackend* backend = BlueZBackend::Instance();
				if (backend != NULL) {
					backend->StopDiscoveryAsync(adapterPath, BMessenger(),
						0);
					backend->StopWatching(BMessenger(this));
				}
			}
			_ReportKnownDevices();
			InquiryCompleted(INQUIRY_COMPLETED);
			break;
		}

		case kInquiryTerminatedWhat:
			InquiryCompleted(INQUIRY_TERMINATED);
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


} // namespace Bluetooth
