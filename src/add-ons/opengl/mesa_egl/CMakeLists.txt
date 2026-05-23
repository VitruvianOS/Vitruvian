AddOn(mesa_egl
	MODULE

	SOURCES
	MesaEGLRenderer.cpp

	LIBS opengl ${EGL_LIBRARIES} ${GBM_LIBRARIES} ${GL_LIBRARIES}
)

set_target_properties(mesa_egl PROPERTIES
	PREFIX ""
	OUTPUT_NAME "mesa_egl"
	SUFFIX ".so"
)

install(TARGETS mesa_egl DESTINATION /system/add-ons/opengl/)
