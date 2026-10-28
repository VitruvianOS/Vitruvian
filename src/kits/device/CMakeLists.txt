# src/kits/device: the USB kit (USBKit.h) lives in libbe, as the legacy
# device kit did. udev enumerates and reports hotplug, libusb-1.0 reads
# descriptors and moves data.
target_sources(be PRIVATE
	USBConfiguration.cpp
	USBDevice.cpp
	USBEndpoint.cpp
	USBInterface.cpp
	USBRoster.cpp
)
