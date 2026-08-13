target_sources(be PRIVATE
	libbluetooth/BlueZBackend.cpp
	libbluetooth/DeviceClass.cpp
	libbluetooth/LocalDevice.cpp
	libbluetooth/RemoteDevice.cpp
	libbluetooth/DiscoveryAgent.cpp
	libbluetooth/DiscoveryListener.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libbluetooth
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