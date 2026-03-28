Application(Screenshot
	SOURCES
	ScreenshotApp.cpp
	ScreenshotWindow.cpp
	SelectAreaView.cpp
	Utility.cpp
	LIBS tracker translation localestub
	RDEF ScreenshotApp.rdef
)
UsePrivateHeaders(Screenshot interface shared)

Application(screenshot_cli
	SOURCES
	Screenshot.cpp
	Utility.cpp
	LIBS translation localestub
	RDEF Screenshot.rdef
)
set_target_properties(screenshot_cli PROPERTIES OUTPUT_NAME screenshot)
UsePrivateHeaders(screenshot_cli interface shared)
