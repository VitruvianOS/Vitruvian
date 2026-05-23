AddOn(
	keyboard
	SHARED

	LIBS
	${INPUT_LIBRARIES} ${EVDEV_LIBRARIES} ${XKBCOMMON_LIBRARIES} be shared localestub

	SOURCES
	KeyboardInputDevice.cpp
	Keymap.cpp
	TeamMonitorWindow.cpp
	TeamListItem.cpp

	INCLUDES
	"./"
	"../"
	${EVDEV_INCLUDES}
	${XKBCOMMON_INCLUDES}
)

UsePrivateHeaders(keyboard input interface shared tracker system)
