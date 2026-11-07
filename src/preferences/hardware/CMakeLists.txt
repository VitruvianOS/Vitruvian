Application(Hardware
	SOURCES
	AccelerationView.cpp
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
	OverclockTemperatures.cpp
	OverclockView.cpp
	RenderingView.cpp
	SysfsRecord.cpp
	UdevScanner.cpp
	LIBS shared localestub tracker udev hwaccel opengl
	RDEF Hardware.rdef
)
DoCatalogs("application/x-vnd.vos-hardware-prefs" preferences/hardware)
