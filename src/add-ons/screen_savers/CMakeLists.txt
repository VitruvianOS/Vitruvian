# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

# Screen saver add-ons. File name on disk is the menu label.
macro(ScreenSaverAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	UsePrivateHeaders(${name} screen_saver interface)
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver
		${PROJECT_SOURCE_DIR}/headers/private/interface
		${PROJECT_SOURCE_DIR}/headers/private/shared
	)
endmacro()

add_subdirectory(butterfly)
add_subdirectory(debugnow)
add_subdirectory(flurry)
add_subdirectory(glife)
add_subdirectory(gravity)
add_subdirectory(icons)
add_subdirectory(ifs)
add_subdirectory(leaves)
add_subdirectory(message)
add_subdirectory(nebula)
add_subdirectory(shelf)
add_subdirectory(simpleclock)
add_subdirectory(slideshowsaver)
add_subdirectory(spider)
