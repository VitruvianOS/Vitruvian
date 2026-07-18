
TranslatorAddOn(
	HVIFTranslator SHARED

	SOURCES
	HVIFMain.cpp
	HVIFTranslator.cpp
	HVIFView.cpp

	LIBS be translation localestub translatorutils

	INCLUDES
	"../shared"

	RDEF HVIFTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-HVIFTranslator" add-ons/translators/hvif)
