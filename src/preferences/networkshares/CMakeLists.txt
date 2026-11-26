Application(NetworkShares
	SOURCES
	NetworkShares.cpp
	NetworkSharesWindow.cpp
	ShareListItem.cpp
	ShareSettings.cpp
	ShareWorker.cpp
	AddShareDialog.cpp
	ShareCore.cpp
	LIBS be shared localestub
	RDEF NetworkShares.rdef
)

target_include_directories(NetworkShares PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(NetworkShares shared)

# Privileged helper; same validation and unit text as the preflet.
add_executable(vos-network-shares-helper
	helper/vos-network-shares-helper.cpp
	ShareCore.cpp
)
target_link_libraries(vos-network-shares-helper PRIVATE be root)
target_link_options(vos-network-shares-helper PRIVATE -Wl,--no-as-needed)
set_target_properties(vos-network-shares-helper PROPERTIES
	COMPILE_FLAGS "-include LinuxBuildCompatibility.h"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libexec"
)
target_include_directories(vos-network-shares-helper PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

DoCatalogs("x-vnd.vos-NetworkShares" preferences/networkshares)
