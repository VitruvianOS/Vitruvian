
TranslatorAddOn(
	RAWTranslator SHARED

	SOURCES
	main.cpp
	RAWTranslator.cpp
	ConfigView.cpp
	RAW.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF RAWTranslator.rdef
)
DoCatalogs("x-vnd.vos-RAWTranslator" add-ons/translators/raw)
