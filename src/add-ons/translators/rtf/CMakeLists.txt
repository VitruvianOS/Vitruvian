
TranslatorAddOn(
	RTFTranslator SHARED

	SOURCES
	ConfigView.cpp
	convert.cpp
	main.cpp
	RTF.cpp
	RTFTranslator.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF RTFTranslator.rdef
)
DoCatalogs("x-vnd.Haiku-RTFTranslator" add-ons/translators/rtf)
