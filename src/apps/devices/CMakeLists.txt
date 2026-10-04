#Application(Devices
#	SOURCES
#	Device.cpp
#	DeviceACPI.cpp
#	DevicePCI.cpp
#	DeviceSCSI.cpp
#	DevicesApplication.cpp
#	DevicesView.cpp
#	DeviceUSB.cpp
#	PropertyList.cpp
#	ResourceUsageWindow.cpp
#	LIBS tracker localestub
#	RDEF Devices.rdef
#)
#UsePrivateHeaders(Devices shared interface system)
DoCatalogs("x-vnd.vos-Devices" apps/devices)
