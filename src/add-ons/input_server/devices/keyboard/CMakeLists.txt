AddOn(
	keyboard
	SHARED

	SOURCES
	KeyboardInputDevice.cpp
	Keymap.cpp
	TeamMonitorWindow.cpp
	TeamListItem.cpp

	INCLUDES
	"./"
	LIBS input_server be shared localestub

	RDEF Keyboard.rdef
)

UsePrivateHeaders(keyboard input interface system tracker)
