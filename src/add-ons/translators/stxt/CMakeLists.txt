
TranslatorAddOn(
	STXTTranslator SHARED

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
DoCatalogs("x-vnd.vos-STXTTranslator" add-ons/translators/stxt)
