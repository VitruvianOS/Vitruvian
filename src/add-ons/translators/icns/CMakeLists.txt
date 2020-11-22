
Application(
	ICNSTranslator

	SOURCES
	ConfigView.cpp
	ICNSLoader.cpp
	ICNSTranslator.cpp
	main.cpp

	LIBS be translation translatorutils ${ICNS_LIBRARIES}

	INCLUDES
	"../shared"
)
