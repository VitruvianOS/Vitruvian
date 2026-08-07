# TODO: MethodReplicant lives in this binary and Deskbar load_add_on()s it via
# INPUTSERVER_SIGNATURE to instantiate the input-method replicant. A plain Server
# is not a dlopen-able ET_DYN, so that lookup fails (grey zombie). Either switch
# this to RunnableAddOn (make the server itself runnable+dlopen-able) or extract
# MethodReplicant into its own add-on .so and archive add_on against that instead.
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
	RDEF input_server.rdef
)

target_compile_definitions(input_server PRIVATE
	-DDEBUG=1 -DTRACE_ADD_ON_MONITOR=1
)

UsePrivateHeaders(input_server app interface system support)

set_target_properties(input_server PROPERTIES ENABLE_EXPORTS 1)
