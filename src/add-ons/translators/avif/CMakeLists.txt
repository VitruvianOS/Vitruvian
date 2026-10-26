TranslatorAddOn(
	AVIFTranslator SHARED

	SOURCES
	AVIFTranslator.cpp
	ConfigView.cpp
	main.cpp

	LIBS be translation translatorutils localestub ${AVIF_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF AVIFTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-AVIFTranslator" add-ons/translators/avif)
