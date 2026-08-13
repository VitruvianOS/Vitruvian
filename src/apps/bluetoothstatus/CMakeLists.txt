RunnableAddOn(BluetoothStatus
	SOURCES
	BluetoothStatus.cpp
	BluetoothStatusView.cpp
	BluetoothStatusWindow.cpp
	PairingDialogWindow.cpp
	LIBS be localestub
	RDEF BluetoothStatus.rdef BluetoothStatusIcons.rdef
)

target_include_directories(BluetoothStatus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/bluetooth/libbluetooth
)
UsePrivateHeaders(BluetoothStatus interface)
DoCatalogs("x-vnd.Haiku-BluetoothStatus" apps/bluetoothstatus)

# Off by default and only settable from the cmake command line -- a debug
# menu that opens the pairing dialog shells with sample content must
# never be reachable in a shipped build.
option(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG
	"Enable the Bluetooth/WiFi dialog-shell test trigger menus" OFF)
if(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG)
	target_compile_definitions(BluetoothStatus PRIVATE
		VITRUVIAN_DIALOG_SHELL_DEBUG)
endif()