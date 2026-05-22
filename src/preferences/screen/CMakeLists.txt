Application(Screen
	SOURCES
	AlertWindow.cpp
	compute_display_timing.cpp
	MonitorView.cpp
	RefreshSlider.cpp
	RefreshWindow.cpp
	ScreenApplication.cpp
	ScreenMode.cpp
	ScreenSettings.cpp
	ScreenWindow.cpp
	Utility.cpp
	LIBS localestub
	RDEF Screen.rdef
)

UsePrivateHeaders(Screen graphics/common preferences)
