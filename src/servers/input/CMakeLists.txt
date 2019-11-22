add_executable(input_server)

target_include_directories(input_server PUBLIC
	"./"
)

target_include_directories(input_server PRIVATE
	"../../../headers/private/interface"
	"../../../headers/os/add-ons/input_server/"
)

target_sources(input_server PRIVATE
	InputServer.cpp
	InputServerDevice.cpp
	InputServerFilter.cpp
	InputServerMethod.cpp

	AddOnManager.cpp

	MouseSettings.cpp
	KeyboardSettings.cpp

	MethodReplicant.cpp
	MethodMenuItem.cpp
	BottomlineWindow.cpp

	PathList.cpp

	# storage
	#AddOnMonitor.cpp
	#AddOnMonitorHandler.cpp
	#NodeMonitorHandler.cpp
)

target_link_libraries(input_server PRIVATE be)
