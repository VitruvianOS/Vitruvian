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
)

UsePrivateHeaders(keyboard input interface)
