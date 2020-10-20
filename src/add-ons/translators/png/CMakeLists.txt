
Application(
	PNGTranslator

	SOURCES
	PNGMain.cpp	
	PNGTranslator.cpp
	PNGView.cpp

	LIBS be translation translatorutils png

	INCLUDES
	"../shared"
	"/usr/include/libpng"
)
