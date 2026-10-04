
TranslatorAddOn(
	WEBPTranslator SHARED

	SOURCES
	ConfigView.cpp
	main.cpp
	WebPTranslator.cpp

	LIBS be translation translatorutils localestub ${WEBP_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF WebPTranslator.rdef
)
DoCatalogs("x-vnd.vos-WebPTranslator" add-ons/translators/webp)
