
TranslatorAddOn(
	ICNSTranslator SHARED

	SOURCES
	ConfigView.cpp
	ICNSLoader.cpp
	ICNSTranslator.cpp
	main.cpp

	LIBS be translation translatorutils localestub ${ICNS_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF ICNSTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-ICNSTranslator" add-ons/translators/icns)
