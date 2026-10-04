
TranslatorAddOn(
	PSDTranslator SHARED

	SOURCES
	ConfigView.cpp	
	DataArray.cpp
	PSDLoader.cpp
	PSDTranslator.cpp
	PSDTranslatorApp.cpp
	PSDWriter.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF PSDTranslator.rdef
)
DoCatalogs("x-vnd.vos-PSDTranslator" add-ons/translators/psd)
