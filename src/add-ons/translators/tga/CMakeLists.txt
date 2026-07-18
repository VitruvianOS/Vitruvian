
TranslatorAddOn(
	TGATranslator SHARED

	SOURCES
	TGAMain.cpp
	TGATranslator.cpp
	TGAView.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF TGATranslator.rdef
)
DoCatalogs("x-vnd.Haiku-TGATranslator" add-ons/translators/tga)
