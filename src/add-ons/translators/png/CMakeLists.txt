
Application(
	PNGTranslator

	SOURCES
	PNGMain.cpp	
	PNGTranslator.cpp
	PNGView.cpp

	LIBS be translation translatorutils localestub ${PNG_LIBRARIES}

	INCLUDES
	"../shared"
	${PNG_INCLUDES}

	RDEF PNGTranslator.rdef
)
