# The telephony kit joins libbe like the network and bluetooth kits;
# BTelephonyRoster/BModem ride on the ModemManager backend and the
# SIP backend, both already compiled into be.
target_sources(be PRIVATE
	libtelephony/Modem.cpp
	libtelephony/TelephonyRoster.cpp
	libtelephony/SipBackend.cpp
	libtelephony/SipVideo.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libtelephony
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
	${CMAKE_SOURCE_DIR}/headers/os/telephony
)

# gio/glib (the backend's D-Bus transport) is declared once in
# src/kits/CMakeLists.txt. libre (the SIP stack) and libvpx (VP8 for
# video calls) are only needed by the telephony kit.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRE REQUIRED libre)
target_include_directories(be PRIVATE ${LIBRE_INCLUDE_DIRS})
target_link_libraries(be PRIVATE ${LIBRE_LIBRARIES})
target_compile_options(be PRIVATE ${LIBRE_CFLAGS_OTHER})

target_link_libraries(be PRIVATE vpx)
