RunnableAddOn(LocationStatus
	SOURCES
	LocationStatus.cpp
	LocationStatusView.cpp
	LocationStatusWindow.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/location/LocationSettings.cpp
	LIBS be shared localestub
	RDEF LocationStatus.rdef
)

target_include_directories(LocationStatus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/preferences/location
)
UsePrivateHeaders(LocationStatus interface shared)
DoCatalogs("x-vnd.vos-LocationStatus" apps/locationstatus)
