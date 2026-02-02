
Application(
	PCXTranslator

	SOURCES
	ConfigView.cpp
	PCXTranslator.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF PCXTranslator.rdef
)
