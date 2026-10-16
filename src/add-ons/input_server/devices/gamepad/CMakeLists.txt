AddOn(
	gamepad
	SHARED

	LIBS
	${EVDEV_LIBRARIES} be

	SOURCES
	GamepadPointerDevice.cpp
	../../../../kits/shared/PointerControlCore.cpp

	INCLUDES
	"./"
	"../"
	${EVDEV_INCLUDES}
)

UsePrivateHeaders(gamepad input interface)
