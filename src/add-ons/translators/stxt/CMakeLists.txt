Application(
	STXTTranslator

	SOURCES
	STXTMain.cpp
	STXTTranslator.cpp
	STXTView.cpp

	LIBS be translation translatorutils

	INCLUDES
	"../shared"
)

UsePrivateHeaders(STXTTranslator textencoding)
