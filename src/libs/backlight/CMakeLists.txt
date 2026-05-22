find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

AddOn(
	backlight
	STATIC

	SOURCES
		libbacklight.c

	INCLUDES
		${DRM_INCLUDE_DIR}/libdrm/
)
