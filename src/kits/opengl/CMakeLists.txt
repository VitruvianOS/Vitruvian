AddOn(opengl
	SHARED

	SOURCES
	GLView.cpp
	GLRenderer.cpp

	LIBS game ${GL_LIBRARIES}
)

install(TARGETS opengl DESTINATION /system/lib/)
