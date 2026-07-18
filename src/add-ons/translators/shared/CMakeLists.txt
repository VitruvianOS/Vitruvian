
TranslatorAddOn(
	translatorutils
	STATIC

	SOURCES
	BaseTranslator.cpp
	StreamBuffer.cpp
	TranslatorSettings.cpp
	TranslatorWindow.cpp
)
DoCatalogs("x-pseudo.libtranslatorsutils" add-ons/translators/shared)
