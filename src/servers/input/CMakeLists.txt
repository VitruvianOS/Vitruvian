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

	../../kits/storage/AddOnMonitor.cpp
	../../kits/storage/AddOnMonitorHandler.cpp
	../../kits/storage/NodeMonitorHandler.cpp

	INCLUDES
	"../../../headers/os/add-ons/input_server/"
	LIBS shared
)

UsePrivateHeaders(input_server app interface system support)
