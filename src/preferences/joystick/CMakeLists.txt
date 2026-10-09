Application(Joystick
	SOURCES
	Joystick.cpp
	JoystickWindow.cpp
	TestPanel.cpp
	CalibrationSettings.cpp
	RumbleTester.cpp
	LIBS be game localestub
	RDEF Joystick.rdef
)

# LinuxEvdevShim.h convention for the rumble helper; never linux/input.h.
target_include_directories(Joystick PRIVATE
	${CMAKE_SOURCE_DIR}/src/add-ons/input_server/devices
	${EVDEV_INCLUDES}
)

DoCatalogs("x-vnd.Haiku-Joystick" preferences/joystick)
