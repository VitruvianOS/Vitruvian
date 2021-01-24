Application(
	STXTTranslator

	SOURCES
	STXTMain.cpp
	STXTTranslator.cpp
	STXTView.cpp

	LIBS be translation translatorutils textencoding localestub

	INCLUDES
	"../shared"
)

UsePrivateHeaders(STXTTranslator textencoding)
