# The telephony kit joins libbe like the network and bluetooth kits;
# BTelephonyRoster/BModem ride on the ModemManager backend already
# compiled into be from the network kit.
target_sources(be PRIVATE
	libtelephony/Modem.cpp
	libtelephony/TelephonyRoster.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libtelephony
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
	${CMAKE_SOURCE_DIR}/headers/os/telephony
)

# gio/glib (the backend's D-Bus transport) is declared once in
# src/kits/CMakeLists.txt.
