AddOn(
	screen_saver
	SHARED

	# ScreenSaverSettings lives in the kit; the filter must link it.
	LIBS
	be
	screensaver

	SOURCES
	ScreenSaverFilter.cpp
)

UsePrivateHeaders(screen_saver input app interface screen_saver)