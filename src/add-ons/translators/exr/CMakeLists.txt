
Application(
	EXRTranslator

	SOURCES
	ConfigView.cpp
	EXRGamma.cpp
	EXRTranslator.cpp
	IStreamWrapper.cpp
	main.cpp

	LIBS be translation translatorutils localestub ${EXR_LIBRARIES}

	INCLUDES
	"../shared"
	${EXR_INCLUDES}

	RDEF EXRTranslator.rdef
)
