AddOn(screensaver
	SHARED

	SOURCES
	DefaultSettingsView.cpp
	ScreenSaver.cpp
	ScreenSaverRunner.cpp
	ScreenSaverSettings.cpp

	LIBS be game
)

UsePrivateHeaders(screensaver screen_saver)
target_include_directories(screensaver PRIVATE ${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver)
