Server(
	input_server

	SOURCES
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

	INCLUDES
	"../../../headers/private/interface"
	"../../../headers/os/add-ons/input_server/"
)
