
TranslatorAddOn(
	PPMTranslator SHARED

	SOURCES
	colorspace.cpp	
	PPMMain.cpp
	PPMTranslator.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF PPMTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-PPMTranslator" add-ons/translators/ppm)
