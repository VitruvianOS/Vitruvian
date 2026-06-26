AddOn(mesa_surfaceless
	MODULE

	SOURCES
	MesaSurfacelessRenderer.cpp

	LIBS opengl ${EGL_LIBRARIES} ${GL_LIBRARIES}
)

install(TARGETS mesa_surfaceless DESTINATION /system/add-ons/opengl/)
