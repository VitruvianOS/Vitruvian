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
