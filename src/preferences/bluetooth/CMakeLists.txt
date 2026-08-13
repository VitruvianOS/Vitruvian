Application(Bluetooth
	SOURCES
	Bluetooth.cpp
	BluetoothSettings.cpp
	BluetoothSettingsView.cpp
	BluetoothWindow.cpp
	DeviceListItem.cpp
	InquiryPanel.cpp
	LIBS be shared localestub
	RDEF Bluetooth.rdef
)

# Kit headers only (headers/os/bluetooth) -- BlueZBackend.h itself must never
# be reachable from here, see src/kits/bluetooth/libbluetooth's own
# CMakeLists.txt for where that boundary is enforced.
target_include_directories(Bluetooth PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

DoCatalogs("x-vnd.Haiku-Bluetooth" preferences/bluetooth)