Application(
	STXTTranslator

	SOURCES
	STXTMain.cpp
	STXTTranslator.cpp
	STXTView.cpp

	LIBS be translation translatorutils textencoding localestub

	INCLUDES
	"../shared"

	RDEF STXTTranslator.rdef
)

UsePrivateHeaders(STXTTranslator textencoding)
