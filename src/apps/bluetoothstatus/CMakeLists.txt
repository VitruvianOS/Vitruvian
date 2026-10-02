RunnableAddOn(BluetoothStatus
	SOURCES
	BluetoothStatus.cpp
	BluetoothStatusView.cpp
	BluetoothStatusWindow.cpp
	PairingDialogWindow.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexReceiveAgent.cpp
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/private/ObexReceivePrompt.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/bluetooth/BluetoothSettings.cpp
	LIBS be shared localestub
	RDEF BluetoothStatus.rdef BluetoothStatusIcons.rdef
)

target_include_directories(BluetoothStatus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/libbluetooth
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
	${CMAKE_SOURCE_DIR}/src/preferences/bluetooth
)
UsePrivateHeaders(BluetoothStatus interface shared)
DoCatalogs("x-vnd.Haiku-BluetoothStatus" apps/bluetoothstatus)

# Off by default and only settable from the cmake command line; a debug
# menu that opens the pairing dialog shells with sample content must
# never be reachable in a shipped build.
option(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG
	"Enable the Bluetooth/WiFi dialog-shell test trigger menus" OFF)
if(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG)
	target_compile_definitions(BluetoothStatus PRIVATE
		VITRUVIAN_DIALOG_SHELL_DEBUG)
endif()
