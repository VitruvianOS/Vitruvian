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
	#GameProducer.cpp        # legacy BBufferProducer node — gone
	GSUtility.cpp
	WindowScreen.cpp
	GameSoundBuffer.cpp
	#PushGameSound.cpp       # Batch E.3

	LIBS media2
)

UsePrivateHeaders(game app interface)
