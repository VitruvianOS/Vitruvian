
TranslatorAddOn(
	TIFFTranslator SHARED

	SOURCES
	TIFFMain.cpp	
	TIFFTranslator.cpp
	TIFFView.cpp

	LIBS be translation translatorutils localestub ${TIFF_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF TIFFTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-TIFFTranslator" add-ons/translators/tiff)
