
Application(
	TIFFTranslator

	SOURCES
	TIFFMain.cpp	
	TIFFTranslator.cpp
	TIFFView.cpp

	LIBS be translation translatorutils ${TIFF_LIBRARIES}

	INCLUDES
	"../shared"
)
