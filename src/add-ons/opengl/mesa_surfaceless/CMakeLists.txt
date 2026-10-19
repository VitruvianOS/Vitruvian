AddOn(mesa_surfaceless
	MODULE

	SOURCES
	MesaSurfacelessRenderer.cpp

	LIBS opengl ${EGL_LIBRARIES} ${GL_LIBRARIES}
)

# Bare file name, same convention as mesa_egl: BGLView's preferred-renderer
# list and the ImageInclude entries address the add-on by file name.
set_target_properties(mesa_surfaceless PROPERTIES
	PREFIX ""
	OUTPUT_NAME "mesa_surfaceless"
	SUFFIX ".so"
)

install(TARGETS mesa_surfaceless DESTINATION /system/add-ons/opengl/)
