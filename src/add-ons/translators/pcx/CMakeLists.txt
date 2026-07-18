
TranslatorAddOn(
	PCXTranslator SHARED

	SOURCES
	ConfigView.cpp
	PCX.cpp
	PCXTranslator.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF PCXTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-PCXTranslator" add-ons/translators/pcx)
