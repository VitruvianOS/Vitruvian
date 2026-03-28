Application(ScreenSaver
	SOURCES
	PasswordWindow.cpp
	PreviewView.cpp
	ScreenCornerSelector.cpp
	ScreenSaverApp.cpp
	ScreenSaverWindow.cpp
	LIBS game screensaver localestub tracker
	RDEF ScreenSaver.rdef
)
UsePrivateHeaders(ScreenSaver screen_saver)
target_include_directories(ScreenSaver PRIVATE
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver)
target_link_libraries(ScreenSaver PUBLIC crypt)
