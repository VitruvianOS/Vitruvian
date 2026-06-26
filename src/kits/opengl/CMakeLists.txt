AddOn(opengl
	SHARED

	SOURCES
	GLView.cpp
	GLRenderer.cpp

	LIBS game ${GL_LIBRARIES}
)

# Install path is wired via build/profiles/base.cmake (SYSTEM_LIBS → /lib).
# A per-target install here used to land libopengl.so in /system/lib/,
# which made postinst's `ln -sf /lib /system/lib` create /system/lib/lib
# inside the existing dir instead of replacing it — leaving every other
# Vitruvian library (libbe, libtracker, …) unreachable at /system/lib/<name>.
