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

UsePrivateHeaders(keyboard app input interface shared tracker system)
DoCatalogs("x-vnd.Haiku-KeyboardInputServerDevice" add-ons/input_server/devices/keyboard)
