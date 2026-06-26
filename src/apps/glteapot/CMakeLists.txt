Application(GLTeapot
	SOURCES
	TeapotApp.cpp
	TeapotWindow.cpp
	ObjectView.cpp
	GLObject.cpp
	FPS.cpp
	error.cpp

	LIBS opengl game ${GL_LIBRARIES} localestub

	RDEF GLTeapot.rdef
)