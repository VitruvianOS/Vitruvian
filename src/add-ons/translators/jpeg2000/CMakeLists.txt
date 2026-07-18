
TranslatorAddOn(
	JPEG2000Translator SHARED

	SOURCES
	JPEG2000Translator.cpp

	LIBS be translation translatorutils localestub ${JPEG_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF JPEG2000Translator.rdef
)
DoCatalogs("x-vnd.Haiku-JPEG2000Translator" add-ons/translators/jpeg2000)
