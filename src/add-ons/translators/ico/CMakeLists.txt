
TranslatorAddOn(
	ICOTranslator SHARED

	SOURCES
	ConfigView.cpp
	ICO.cpp
	ICOTranslator.cpp
	main.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF ICOTranslator.rdef
)
DoCatalogs("x-vnd.vos-ICOTranslator" add-ons/translators/ico)
