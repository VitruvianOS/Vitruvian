# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(MessageSaver
	SOURCES
	Message.cpp

	LIBS be localestub
	RDEF Message.rdef
)

# Menu label is "message"; cmake target cannot share the bin/message app name.
set_target_properties(MessageSaver PROPERTIES OUTPUT_NAME message)
