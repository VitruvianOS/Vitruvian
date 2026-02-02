
Application(
	BMPTranslator

	SOURCES
	BMPMain.cpp
	BMPTranslator.cpp
	BMPView.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF BMPTranslator.rdef
)
