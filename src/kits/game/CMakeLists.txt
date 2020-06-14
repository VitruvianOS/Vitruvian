AddOn(game
	SHARED

	SOURCES
	DirectWindow.cpp
	#GameSound.cpp
	#SimpleGameSound.cpp
	#FileGameSound.cpp
	#GameSoundDevice.cpp
	#StreamingGameSound.cpp
	#GameProducer.cpp
	#GSUtility.cpp
	WindowScreen.cpp
	#GameSoundBuffer.cpp
	#PushGameSound.cpp
)

UsePrivateHeaders(game app interface)
