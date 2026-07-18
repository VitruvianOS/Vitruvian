
TranslatorAddOn(
	BMPTranslator SHARED

	SOURCES
	BMPMain.cpp
	BMPTranslator.cpp
	BMPView.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF BMPTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-BMPTranslator" add-ons/translators/bmp)
