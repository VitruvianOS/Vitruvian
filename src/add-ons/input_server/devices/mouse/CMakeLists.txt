AddOn(
	mouse
	SHARED

	LIBS
	${FREETYPE_LIBRARIES} ${INPUT_LIBRARIES} ${UDEV_LIBRARIES} ${DRM_LIBRARIES} ${EVDEV_LIBRARIES} ${LIBINPUT_LIBRARIES} be

	SOURCES
	MouseInputDevice.cpp
	movement_maker.cpp

	INCLUDES
	"./"
	"../"
	${EVDEV_INCLUDES}
	${LIBINPUT_INCLUDE_DIRS}
)

UsePrivateHeaders(keyboard input interface)
