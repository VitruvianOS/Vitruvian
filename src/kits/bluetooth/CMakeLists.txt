target_sources(be PRIVATE
	libbluetooth/BlueZBackend.cpp
	libbluetooth/DeviceClass.cpp
	libbluetooth/LocalDevice.cpp
	libbluetooth/ObexClient.cpp
	libbluetooth/RemoteDevice.cpp
	libbluetooth/DiscoveryAgent.cpp
	libbluetooth/DiscoveryListener.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libbluetooth
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)

# Find BlueZ libraries
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(BLUEZ REQUIRED bluez)
	if(BLUEZ_FOUND)
		target_include_directories(be PRIVATE ${BLUEZ_INCLUDE_DIRS})
		target_link_libraries(be PRIVATE ${BLUEZ_LIBRARIES})
		target_compile_options(be PRIVATE ${BLUEZ_CFLAGS_OTHER})
	endif()

	# gio/glib (BlueZBackend.cpp's D-Bus transport) is declared once in
	# src/kits/CMakeLists.txt, shared with the network kit's libnm.
endif()
# UI pieces shared by the Bluetooth preflet, BluetoothStatus and the
# SendToBluetooth Tracker add-on: OBEX send/receive windows and settings.
AddOn(
	bluetoothui
	STATIC
	SOURCES
	private/BluetoothSettings.cpp
	private/ObexReceiveAgent.cpp
	private/ObexReceivePrompt.cpp
	private/ObexSendChooser.cpp
	private/ObexTransferWindow.cpp
)

target_include_directories(bluetoothui PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)
UsePrivateHeaders(bluetoothui shared)
