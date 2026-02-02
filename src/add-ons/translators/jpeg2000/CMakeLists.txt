
Application(
	JPEG2000Translator

	SOURCES
	JPEG2000Translator.cpp

	LIBS be translation translatorutils localestub ${JPEG_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF JPEG2000Translator.rdef
)
