# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

Application(MediaConverter
	SOURCES
	MediaConverterApp.cpp
	MediaConverterWindow.cpp
	MediaEncoderWindow.cpp
	MediaFileInfo.cpp
	MediaFileInfoView.cpp
	MediaFileListView.cpp

	LIBS media2 tracker localestub

	RDEF MediaConverter.rdef
)
