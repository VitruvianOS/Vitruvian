Application(screen_blanker
	SOURCES
	ScreenBlanker.cpp
	ScreenSaverWindow.cpp
	PasswordWindow.cpp
	../../kits/screensaver/ScreenSaver.cpp

	LIBS game localestub screensaver
	RDEF screen_blanker.rdef
)

UsePrivateHeaders(screen_blanker interface screen_saver)
