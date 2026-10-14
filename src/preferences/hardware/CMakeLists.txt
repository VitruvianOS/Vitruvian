Application(Hardware
	SOURCES
	DeviceClassifier.cpp
	DeviceListItem.cpp
	DeviceTreeView.cpp
	EdidParser.cpp
	HardwareApp.cpp
	HardwareHelperClient.cpp
	HardwareModel.cpp
	HardwareReport.cpp
	HardwareView.cpp
	HardwareWindow.cpp
	HwdataDatabase.cpp
	MachineInfoReader.cpp
	ModaliasParser.cpp
	SysfsRecord.cpp
	UdevScanner.cpp
	LIBS shared localestub tracker udev
	RDEF Hardware.rdef
)
DoCatalogs("application/x-vnd.vos-hardware-prefs" preferences/hardware)
