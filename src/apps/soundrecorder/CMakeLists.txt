# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
Application(SoundRecorder
	SOURCES
	DrawButton.cpp
	DrawingTidbits.cpp
	FileUtils.cpp
	RecorderApp.cpp
	RecorderWindow.cpp
	ScopeView.cpp
	SoundListView.cpp
	TrackSlider.cpp
	TransportButton.cpp
	UpDownButton.cpp
	VUView.cpp
	VolumeSlider.cpp
	LIBS be shared localestub media2 tracker
	RDEF SoundRecorder.rdef
)

target_include_directories(SoundRecorder PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)
UsePrivateHeaders(SoundRecorder interface shared)
DoCatalogs("x-vnd.Haiku-SoundRecorder" apps/soundrecorder)
