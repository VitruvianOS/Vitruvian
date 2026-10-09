AddOn(game
	SHARED

	SOURCES
	DirectWindow.cpp
	GameSound.cpp
	SimpleGameSound.cpp
	FileGameSound.cpp
	GameSoundDevice.cpp
	StreamingGameSound.cpp
	PushGameSound.cpp
	GSUtility.cpp
	WindowScreen.cpp
	GameSoundBuffer.cpp
	Joystick.cpp

	LIBS media2 be ${EVDEV_LIBRARIES}

	INCLUDES
	"${CMAKE_CURRENT_SOURCE_DIR}/../../add-ons/input_server/devices/"
	${EVDEV_INCLUDES}
)

UsePrivateHeaders(game app interface)
