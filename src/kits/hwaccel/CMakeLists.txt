# Hardware acceleration kit: OpenGL renderer priority list
# (~/config/settings/opengl/render_priority) and renderer selection
# (~/config/settings/opengl/renderer_selection), shared by the Hardware
# preflet and BGLView. Installed via build/profiles/base.cmake (SYSTEM_LIBS).
AddOn(hwaccel
	SHARED

	SOURCES
	GLRendererPriority.cpp
	GLRendererSelect.cpp
	GLRendererSelectScore.cpp

	LIBS be
)
