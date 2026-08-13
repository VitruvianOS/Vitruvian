/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_LIST_ITEM_H
#define DEVICE_LIST_ITEM_H


#include <ListItem.h>
#include <String.h>

#include <bluetooth/RemoteDevice.h>


// Two-line row renderer for a paired/available device: class icon, friendly
// name, address and a paired/connected badge. Port of haiku-latest's
// DeviceListItem, rebuilt on RemoteDevice (BlueZ path identity) instead of
// BluetoothDevice/bdaddr_t.
class DeviceListItem : public BListItem {
public:
	explicit DeviceListItem(const Bluetooth::RemoteDevice& device);
	virtual ~DeviceListItem();

	virtual void DrawItem(BView* owner, BRect frame, bool complete = false);
	virtual void Update(BView* owner, const BFont* font);

	const BString& Path() const { return fPath; }
	bool IsPaired() const { return fPaired; }
	bool IsConnected() const { return fConnected; }
	bool IsTrusted() const { return fTrusted; }
	bool IsBlocked() const { return fBlocked; }

	void UpdateFrom(const Bluetooth::RemoteDevice& device);

private:
	BString fPath;
	BString fAddress;
	BString fName;
	Bluetooth::DeviceClass fClass;
	bool fPaired;
	bool fConnected;
	bool fTrusted;
	bool fBlocked;
};


#endif // DEVICE_LIST_ITEM_H
