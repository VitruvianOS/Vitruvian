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

	LIBS media2
)

UsePrivateHeaders(game app interface)
