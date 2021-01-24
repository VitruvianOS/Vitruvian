Application(
	WEBPTranslator

	SOURCES
	ConfigView.cpp
	main.cpp
	WebPTranslator.cpp

	LIBS be translation translatorutils localestub ${WEBP_LIBRARIES}

	INCLUDES
	"../shared"
)
