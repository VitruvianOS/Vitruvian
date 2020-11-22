
Application(
	PNGTranslator

	SOURCES
	PNGMain.cpp	
	PNGTranslator.cpp
	PNGView.cpp

	LIBS be translation translatorutils ${PNG_LIBRARIES}

	INCLUDES
	"../shared"
	${PNG_INCLUDES}
)
