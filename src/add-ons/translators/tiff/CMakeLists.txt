
Application(
	TIFFTranslator

	SOURCES
	TIFFMain.cpp	
	TIFFTranslator.cpp
	TIFFView.cpp

	LIBS be translation translatorutils localestub ${TIFF_LIBRARIES}

	INCLUDES
	"../shared"
)
