AddOn(
	translation
	SHARED

	SOURCES
	BitmapStream.cpp
	FuncTranslator.cpp
	TranslationUtils.cpp
	Translator.cpp
	TranslatorRoster.cpp
	LIBS textencoding
)

UsePrivateHeaders(translation textencoding)
