
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
