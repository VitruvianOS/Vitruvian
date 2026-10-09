Application(Bluetooth
	SOURCES
	Bluetooth.cpp
	BluetoothSettingsView.cpp
	BluetoothWindow.cpp
	DeviceListItem.cpp
	InquiryPanel.cpp
	LIBS be bluetoothui tracker shared localestub
	RDEF Bluetooth.rdef
)

# Kit headers only (headers/os/bluetooth). BlueZBackend.h must never be
# reachable from here; the OBEX facade lives under headers/private/bluetooth.
target_include_directories(Bluetooth PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)
UsePrivateHeaders(Bluetooth bluetooth shared)
DoCatalogs("x-vnd.Haiku-Bluetooth" preferences/bluetooth)
