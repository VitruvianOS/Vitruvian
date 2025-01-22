AddOn(
	mouse
	SHARED

	LIBS
	${FREETYPE_LIBRARIES} ${INPUT_LIBRARIES} ${UDEV_LIBRARIES} ${DRM_LIBRARIES}

	SOURCES
	MouseInputDevice.cpp

	INCLUDES
	"./"
)

UsePrivateHeaders(keyboard input interface)
