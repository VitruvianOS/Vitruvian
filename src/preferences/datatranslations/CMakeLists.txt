Application(DataTranslations
	SOURCES
	DataTranslations.cpp
	DataTranslationsSettings.cpp
	DataTranslationsWindow.cpp
	TranslatorListView.cpp
	LIBS shared translation localestub
	RDEF DataTranslations.rdef
)
UsePrivateHeaders(DataTranslations shared)
