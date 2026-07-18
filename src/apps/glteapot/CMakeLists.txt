Application(GLTeapot
	SOURCES
	TeapotApp.cpp
	TeapotWindow.cpp
	ObjectView.cpp
	GLObject.cpp
	FPS.cpp
	error.cpp

	LIBS opengl GL GLU game localestub

	RDEF GLTeapot.rdef
)

install(TARGETS GLTeapot DESTINATION /system/apps/)
DoCatalogs("x-vnd.Haiku-GLTeapot" apps/glteapot)
