RunnableAddOn(NetworkStatus
	SOURCES
	NetworkStatus.cpp
	NetworkStatusViewNM.cpp
	NetworkStatusWindow.cpp
	RadioView.cpp
	WirelessNetworkMenuItem.cpp
	ConnectionInfoWindow.cpp
	SecretDialogWindow.cpp
	LIBS be localestub
	RDEF NetworkStatus.rdef NetworkStatusIcons.rdef
)

target_include_directories(NetworkStatus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/network/libnetapi
)
UsePrivateHeaders(NetworkStatus interface net)
DoCatalogs("x-vnd.Haiku-NetworkStatus" apps/networkstatus)

# Off by default and only settable from the cmake command line -- a debug
# menu that opens the secret dialog shells with sample content must
# never be reachable in a shipped build.
option(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG
	"Enable the Bluetooth/WiFi dialog-shell test trigger menus" OFF)
if(VITRUVIAN_ENABLE_DIALOG_SHELL_DEBUG)
	target_compile_definitions(NetworkStatus PRIVATE
		VITRUVIAN_DIALOG_SHELL_DEBUG)
endif()

# Shared network library components are now part of the main libnetapi