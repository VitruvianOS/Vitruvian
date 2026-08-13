/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Divergences from haiku-latest's BluetoothSettingsView, see
 * BluetoothSettingsView.cpp for the reasoning behind each:
 *  - No "Identify host as" control -- org.bluez.Adapter1.Class is read-only
 *    over D-Bus (Class= in /etc/bluetooth/main.conf, reloaded by bluetoothd),
 *    so the class is shown as read-only text instead of an editable menu.
 *  - "Incoming connections policy" is a single Pairable checkbox, not
 *    upstream's three-way menu -- BlueZ has no policy tiers, only an
 *    Adapter1.Pairable on/off gate, and it is live-written like Powered/
 *    Discoverable elsewhere in this preflet (not something to Apply/Revert).
 */
#ifndef BLUETOOTH_SETTINGS_VIEW_H
#define BLUETOOTH_SETTINGS_VIEW_H

#include "BluetoothSettings.h"

#include <ObjectList.h>
#include <View.h>

class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BStringView;

namespace Bluetooth {
	class LocalDevice;
	typedef BObjectList<LocalDevice, true> LocalDevicesList;
}


class BluetoothSettingsView : public BView {
public:
	// settings is owned by the caller (BluetoothWindow) and outlives this
	// view -- shared rather than each holding a separate copy, so a change
	// made here (e.g. inquiry time) is visible immediately to the "Add..."
	// button on the devices tab within the same session, not just after a
	// save/reload round trip.
								BluetoothSettingsView(const char* name,
									BluetoothSettings& settings);
	virtual						~BluetoothSettingsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_RequestStatus();
			void				_ApplyStatus(BMessage* message);
			void				_SelectAdapter(const BString& path);
			Bluetooth::LocalDevice* _SelectedAdapter();
			void				_UpdateAdapterDependentControls();

			BluetoothSettings&	fSettings;

			BMenuField*			fLocalDevicesMenuField;
			BPopUpMenu*			fLocalDevicesMenu;

			BSlider*			fInquiryTimeControl;
			BCheckBox*			fPairableCheckBox;
			BStringView*		fDeviceClassView;

			Bluetooth::LocalDevicesList* fAdapters;
			BString				fSelectedAdapterPath;
};

#endif // BLUETOOTH_SETTINGS_VIEW_H
