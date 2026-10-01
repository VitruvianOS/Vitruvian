# StickIt — interactive BJoystick demo from "Getting a Grip on BJoystick".
# Compiles against the public BJoystick API (headers/os/device/Joystick.h)
# implemented by the Game Kit's evdev backend; consumers link `game`.

Test(
	StickIt

	SOURCES
	StickItApp.cpp
	StickItWindow.cpp
	JoystickWindow.cpp

	LIBS
	game
)
