# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
Application(Camera
	SOURCES
		AudioCapture.cpp
		CameraApp.cpp
		CameraView.cpp
		CameraWindow.cpp
		PhotoWriter.cpp
	LIBS be media2 translation
	RDEF Camera.rdef
)

target_include_directories(Camera PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(Camera shared)
