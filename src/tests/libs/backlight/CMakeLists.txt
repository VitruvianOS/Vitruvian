# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

Test(
	test_backlight_selection
	SOURCES
		test_backlight_selection.c
		${PROJECT_SOURCE_DIR}/src/libs/backlight/libbacklight.c
	INCLUDES
		${PROJECT_SOURCE_DIR}/src/libs/backlight/
		${DRM_INCLUDE_DIR}/libdrm/
	LIBS
		udev
)

Test(
	test_drm_card_rank
	SOURCES
		test_drm_card_rank.cpp
	INCLUDES
		${PROJECT_SOURCE_DIR}/headers/private/
		${DRM_INCLUDE_DIR}/libdrm/
	LIBS
		seat
)
