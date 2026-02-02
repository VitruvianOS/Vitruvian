
Application(
	ICOTranslator

	SOURCES
	ConfigView.cpp
	ICO.cpp
	ICOTranslator.cpp
	main.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF ICOTranslator.rdef
)
