Application(Bluetooth
	SOURCES
	Bluetooth.cpp
	BluetoothSettings.cpp
	BluetoothSettingsView.cpp
	BluetoothWindow.cpp
	DeviceListItem.cpp
	InquiryPanel.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexTransferWindow.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexSendChooser.cpp
	LIBS be tracker shared localestub
	RDEF Bluetooth.rdef
)

# Kit headers only (headers/os/bluetooth). BlueZBackend.h must never be
# reachable from here; the OBEX facade lives under headers/private/bluetooth.
target_include_directories(Bluetooth PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/libbluetooth
)
UsePrivateHeaders(Bluetooth bluetooth shared)
DoCatalogs("x-vnd.Haiku-Bluetooth" preferences/bluetooth)
