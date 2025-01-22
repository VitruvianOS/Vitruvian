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

target_compile_definitions(input_server PRIVATE
	-DDEBUG=1 -DTRACE_ADD_ON_MONITOR=1
)

UsePrivateHeaders(input_server app interface system support)

set_target_properties(input_server PROPERTIES ENABLE_EXPORTS 1)
