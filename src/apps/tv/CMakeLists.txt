# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
Application(TV
	SOURCES
	Controller.cpp
	MainApp.cpp
	MainWin.cpp
	VideoView.cpp
	LIBS be shared localestub media2
	RDEF tv.rdef
)

target_include_directories(TV PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(TV interface shared)
# Intentionally not added to build/profiles/apps.cmake: media2 has no
# video capture path yet; see Controller.cpp.
