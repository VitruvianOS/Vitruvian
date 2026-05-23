AddOn(
	libinput_device
	SHARED

	LIBS
	${INPUT_LIBRARIES} ${UDEV_LIBRARIES} ${EVDEV_LIBRARIES} be

	SOURCES
	LibinputDevice.cpp

	INCLUDES
	"./"
	"../"
	${EVDEV_INCLUDES}
)

UsePrivateHeaders(keyboard input interface)
