find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

AddOn(opengl
	SHARED

	SOURCES
	GLView.cpp
	GLRenderer.cpp
	GlDeviceSelect.cpp

	LIBS game ${GL_LIBRARIES} ${DRM_LIBRARIES}
)

# GlDeviceSelect.cpp reads DRM device metadata (xf86drm.h) for GPU
# selection; the header itself stays libdrm-free for host tests.
target_include_directories(opengl PRIVATE ${DRM_INCLUDE_DIR}/libdrm/)

# Install path is wired via build/profiles/base.cmake (SYSTEM_LIBS → /lib).
# A per-target install here used to land libopengl.so in /system/lib/,
# which made postinst's `ln -sf /lib /system/lib` create /system/lib/lib
# inside the existing dir instead of replacing it — leaving every other
# Vitruvian library (libbe, libtracker, …) unreachable at /system/lib/<name>.
