# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

Application(screen_blanker
	SOURCES
	ScreenBlanker.cpp
	ScreenSaverWindow.cpp

	LIBS game localestub screensaver
	RDEF screen_blanker.rdef
)

UsePrivateHeaders(screen_blanker interface screen_saver app)
target_include_directories(screen_blanker PRIVATE
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver
)
DoCatalogs("x-vnd.Haiku.screenblanker" bin/screen_blanker)
