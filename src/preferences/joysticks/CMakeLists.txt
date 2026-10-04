Application(Joysticks
	SOURCES
	Joysticks.cpp
	JoystickWindow.cpp
	TestPanel.cpp
	CalibrationSettings.cpp
	RumbleTester.cpp
	LIBS be game localestub
	RDEF Joysticks.rdef
)

# LinuxEvdevShim.h convention for the rumble helper; never linux/input.h.
target_include_directories(Joysticks PRIVATE
	${CMAKE_SOURCE_DIR}/src/add-ons/input_server/devices
	${EVDEV_INCLUDES}
)

DoCatalogs("x-vnd.vos-Joysticks" preferences/joysticks)
